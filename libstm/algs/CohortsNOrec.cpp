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
#include "../algs.hpp"
#include "../RedoRAWUtils.hpp"

// define atomic operations
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch

using stm::TxThread;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::ValueList;
using stm::ValueListEntry;

using stm::started;
using stm::cpending;
using stm::committed;
using stm::last_complete;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  const uintptr_t VALIDATION_FAILED = 1;
  NOINLINE bool validate(TxThread* tx);

  struct CohortsNOrec {
      static TM_FASTCALL bool begin();
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro();
      static TM_FASTCALL void commit_rw();

      static void rollback(STM_ROLLBACK_SIG(,,));
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
  CohortsNOrec::begin()
  {
      TxThread* tx = stm::Self;
      tx->allocator.onTxBegin();

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

      return true;
  }

  /**
   *  CohortsNOrec commit (read-only):
   */
  void
  CohortsNOrec::commit_ro()
  {
      TxThread* tx = stm::Self;
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
  CohortsNOrec::commit_rw()
  {
      TxThread* tx = stm::Self;
      // order of first tx in cohort
      uint32_t first = last_complete.val + 1;
      CFENCE;

      tx->order = ADD(&cpending.val, 1);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // get the lock and validate
      if (tx->order != first)
          if (!validate(tx)) {
              committed.val++;
              CFENCE;
              last_complete.val = tx->order;
              tx->tmabort();
          }

      // Wait until all tx are ready to commit
      while (cpending.val < started.val);

      // do write back
      tx->writes.writeback();

      // increase total number of committed tx
      committed.val++;
      CFENCE;

      // set myself as last completed tx
      last_complete.val = tx->order;

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsNOrec read (read-only transaction)
   */
  void*
  CohortsNOrec::read_ro(STM_READ_SIG(addr,))
  {
      TxThread* tx = stm::Self;
      void * tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsNOrec read (writing transaction)
   */
  void*
  CohortsNOrec::read_rw(STM_READ_SIG(addr,mask))
  {
      TxThread* tx = stm::Self;
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
  void CohortsNOrec::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  CohortsNOrec write (writing context)
   */
  void CohortsNOrec::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsNOrec unwinder:
   */
  void
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

      PostRollback(tx);
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
   */
  bool
  validate(TxThread* tx)
  {
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid) return false;
      }
      return true;
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
      last_complete.val = 0;
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

