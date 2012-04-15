/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

/**
 *  CohortsNOrec Implementation
 *
 *  Cohorts NOrec version.
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

// define atomic operations
#define CAS __sync_val_compare_and_swap
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch

using stm::TxThread;
using stm::timestamp;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::ValueList;
using stm::ValueListEntry;

using stm::started;
using stm::cpending;
using stm::committed;
using stm::last_order;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  const uintptr_t VALIDATION_FAILED = 1;
  NOINLINE uintptr_t validate(TxThread* tx);

  struct CohortsNOrec {
      static TM_FASTCALL bool begin(TxThread*);
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void commit_ro(TxThread* tx);
      static TM_FASTCALL void commit_rw(TxThread* tx);

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
  };

  /**
   *  CohortsNOrec begin:
   *  CohortsNOrec has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  bool
  CohortsNOrec::begin(TxThread* tx)
  {
    S1:
      // wait until everyone is committed
      while (cpending.val != committed.val);

      // before tx begins, increase total number of tx
      ADD(&started.val, 1);

      // [NB] we must double check no one is ready to commit yet!
      if (cpending.val > committed.val) {
          SUB(&started.val, 1);
          goto S1;
      }

      // Sample the sequence lock, if it is even decrement by 1
      tx->start_time = timestamp.val & ~(1L);

      tx->allocator.onTxBegin();

      return true;
  }

  /**
   *  CohortsNOrec commit (read-only):
   */
  void
  CohortsNOrec::commit_ro(TxThread* tx)
  {
      // decrease total number of tx started
      SUB(&started.val, 1);

      // clean up
      tx->vlist.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  CohortsNOrec commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsNOrec::commit_rw(TxThread* tx)
  {
    ADD(&cpending.val, 1);

    // Wait until all tx are ready to commit
    while (cpending.val < started.val);

    // [mfs] this is over-synchronized.  If we kept the return value of the
    //       above ADD, we could simply use it as the order.  Also, note that
    //       if we did that, the first thread would not need to validate.

    // get the lock and validate (use RingSTM obstruction-free technique)
    while (!bcasptr(&timestamp.val, tx->start_time, tx->start_time + 1))
        if ((tx->start_time = validate(tx)) == VALIDATION_FAILED) {
            ADD(&committed.val, 1);
            tx->tmabort(tx);
        }

    // do write back
    tx->writes.writeback();

    // Release the sequence lock, then clean up
    CFENCE;
    timestamp.val = tx->start_time + 2;

    // increase total number of committed tx
    //
    // [mfs] if we used this as the indicator for when the next one could start
    //       validating, we wouldn't need timestamp and we wouldn't need an
    //       atomic op here.
    ADD(&committed.val, 1);

    // commit all frees, reset all lists
    tx->vlist.reset();
    tx->writes.reset();
    OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsNOrec read (read-only transaction)
   */
  void*
  CohortsNOrec::read_ro(STM_READ_SIG(tx,addr,))
  {
      void * tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsNOrec read (writing transaction)
   */
  void*
  CohortsNOrec::read_rw(STM_READ_SIG(tx,addr,mask))
  {
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      void* tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      REDO_RAW_CLEANUP(tmp, found, log, mask);
      return tmp;
  }

  /**
   *  CohortsNOrec write (read-only context): for first write
   */
  void
  CohortsNOrec::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  CohortsNOrec write (writing context)
   */
  void
  CohortsNOrec::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsNOrec unwinder:
   */
  stm::scope_t*
  CohortsNOrec::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists
      tx->vlist.reset();
      tx->writes.reset();

      return PostRollback(tx);
  }

  /**
   *  CohortsNOrec in-flight irrevocability:
   */
  bool
  CohortsNOrec::irrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsNOrec Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsNOrec validation for commit: check that all reads are valid
   *
   *  [mfs] We should be able to validate without any checks of the
   *        timestamp...
   */
  uintptr_t
  validate(TxThread* tx)
  {
      while (true) {
          // read the lock until it is even
          uintptr_t s = timestamp.val;
          if ((s & 1) == 1)
              continue;

          // check the read set
          CFENCE;
          // don't branch in the loop---consider it backoff if we fail
          // validation early
          bool valid = true;
          foreach (ValueList, i, tx->vlist)
              valid &= STM_LOG_VALUE_IS_VALID(i, tx);

          if (!valid)
              return VALIDATION_FAILED;

          // restart if timestamp changed during read set iteration
          CFENCE;
          if (timestamp.val == s)
              return s;

      }
  }

  /**
   *  Switch to CohortsNOrec:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   */
  void
  CohortsNOrec::onSwitchTo()
  {
    if (timestamp.val & 1)
      ++timestamp.val;
  }
}

namespace stm {
  /**
   *  CohortsNOrec initialization
   */
  template<>
  void initTM<CohortsNOrec>()
  {
      // set the name
      stms[CohortsNOrec].name      = "CohortsNOrec";
      // set the pointers
      stms[CohortsNOrec].begin     = ::CohortsNOrec::begin;
      stms[CohortsNOrec].commit    = ::CohortsNOrec::commit_ro;
      stms[CohortsNOrec].read      = ::CohortsNOrec::read_ro;
      stms[CohortsNOrec].write     = ::CohortsNOrec::write_ro;
      stms[CohortsNOrec].rollback  = ::CohortsNOrec::rollback;
      stms[CohortsNOrec].irrevoc   = ::CohortsNOrec::irrevoc;
      stms[CohortsNOrec].switcher  = ::CohortsNOrec::onSwitchTo;
      stms[CohortsNOrec].privatization_safe = true;
  }
}

