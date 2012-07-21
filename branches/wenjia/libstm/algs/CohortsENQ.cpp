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
 *  CohortsENQ Implementation
 *
 *  CohortsENQ is CohortsNorec with inplace write if I'm the last one in the
 *  cohort. Use queue to handle ordered commit.
 */

#include "../profiling.hpp"
#include "../algs.hpp"
#include "../RedoRAWUtils.hpp"

// define atomic operations
#define CAS __sync_val_compare_and_swap
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch

using stm::TxThread;
using stm::WriteSet;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::ValueList;
using stm::ValueListEntry;
using stm::started;
using stm::cohorts_node_t;

#define NOTDONE 0
#define DONE 1

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  volatile uint32_t inplace = 0;
  NOINLINE bool validate(TxThread* tx);
  // global linklist's head
  struct cohorts_node_t* volatile q = NULL;

  struct CohortsENQ {
      static TM_FASTCALL bool begin();
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,));
      static TM_FASTCALL void* read_turbo(STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_turbo(STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro();
      static TM_FASTCALL void commit_rw();
      static TM_FASTCALL void commit_turbo();

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
  };

  /**
   *  CohortsENQ begin:
   *  CohortsENQ has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  bool
  CohortsENQ::begin()
  {
      TxThread* tx = stm::Self;
      tx->allocator.onTxBegin();
    S1:
      // wait until everyone is committed
      while (q != NULL);

      // before tx begins, increase total number of tx
      ADD(&started.val, 1);

      // [NB] we must double check no one is ready to commit yet
      // and no one entered in place write phase(turbo mode)
      if (q != NULL|| inplace == 1){
          SUB(&started.val, 1);
          goto S1;
      }

      // reset local turn val
      tx->turn.val = NOTDONE;

      return true;
  }

  /**
   *  CohortsENQ commit (read-only):
   */
  void
  CohortsENQ::commit_ro()
  {
      TxThread* tx = stm::Self;
      // decrease total number of tx started
      SUB(&started.val, 1);

      // clean up
      tx->vlist.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  CohortsENQ commit (in place write commit): no validation, no write back
   *  no other thread touches cpending
   */
  void
  CohortsENQ::commit_turbo()
  {
      TxThread* tx = stm::Self;
      // decrease total number of tx started
      SUB(&started.val, 1);

      // clean up
      tx->vlist.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);

      // wait for tx in commit_rw finish
      while (q != NULL);

      // reset in place write flag
      inplace = 0;
  }

  /**
   *  CohortsENQ commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsENQ::commit_rw()
  {
      TxThread* tx = stm::Self;
      // add myself to the queue
      do {
          tx->turn.next = q;
      }while (!bcasptr(&q, tx->turn.next, &(tx->turn)));

      // decrease total number of tx started
      SUB(&started.val , 1);

      // wait for my turn
      if (tx->turn.next != NULL)
          while (tx->turn.next->val != DONE);

      // Wait until all tx are ready to commit
      while (started.val != 0);

      // If in place write occurred, all tx validate reads
      // Otherwise, only first one skips validation
      if (inplace == 1 || tx->turn.next != NULL)
          if (!validate(tx)) {
              // mark self done
              tx->turn.val = DONE;
              // reset q if last one
              if (q == &(tx->turn)) q = NULL;
              // abort
              tx->tmabort(tx);
          }

      // do write back
      tx->writes.writeback();
      CFENCE;

      // mark self as done
      tx->turn.val = DONE;

      // last one in cohort reset q
      if (q == &(tx->turn))
          q = NULL;

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsENQ read_turbo
   */
  void*
  CohortsENQ::read_turbo(STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsENQ read (read-only transaction)
   */
  void*
  CohortsENQ::read_ro(STM_READ_SIG(addr,))
  {
      TxThread* tx = stm::Self;
      void *tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsENQ read (writing transaction)
   */
  void*
  CohortsENQ::read_rw(STM_READ_SIG(addr,mask))
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
   *  CohortsENQ write (read-only context): for first write
   */
  void
  CohortsENQ::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // If everyone else is ready to commit, do in place write
      if (started.val == 1) {
          // set up flag indicating in place write starts
          atomicswap32(&inplace, 1);
          // double check is necessary
          if (started.val == 1) {
              // in place write
              *addr = val;
              // go turbo mode
              OnFirstWrite(tx, read_turbo, write_turbo, commit_turbo);
              return;
          }
          // reset flag
          inplace = 0;
      }
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  CohortsENQ write (in place write)
   */
  void
  CohortsENQ::write_turbo(STM_WRITE_SIG(addr,val,mask))
  {
      *addr = val; // in place write
  }

  /**
   *  CohortsENQ write (writing context)
   */
  void
  CohortsENQ::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsENQ unwinder:
   */
  stm::scope_t*
  CohortsENQ::rollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsENQ in-flight irrevocability:
   */
  bool
  CohortsENQ::irrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsENQ Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsENQ validation for commit: check that all reads are valid
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
   *  Switch to CohortsENQ:
   *
   */
  void
  CohortsENQ::onSwitchTo()
  {
      inplace = 0;
  }
}

namespace stm {
  /**
   *  CohortsENQ initialization
   */
  template<>
  void initTM<CohortsENQ>()
  {
      // set the name
      stms[CohortsENQ].name      = "CohortsENQ";
      // set the pointers
      stms[CohortsENQ].begin     = ::CohortsENQ::begin;
      stms[CohortsENQ].commit    = ::CohortsENQ::commit_ro;
      stms[CohortsENQ].read      = ::CohortsENQ::read_ro;
      stms[CohortsENQ].write     = ::CohortsENQ::write_ro;
      stms[CohortsENQ].rollback  = ::CohortsENQ::rollback;
      stms[CohortsENQ].irrevoc   = ::CohortsENQ::irrevoc;
      stms[CohortsENQ].switcher  = ::CohortsENQ::onSwitchTo;
      stms[CohortsENQ].privatization_safe = true;
  }
}

