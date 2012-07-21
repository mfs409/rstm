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
 *  CohortsEN2Q Implementation
 *
 *  CohortsEN2Q is CohortsNorec with inplace write if I'm the last one in the
 *  cohort. (Relexed CONDITION TO GO TURBO.)
 *  Use Queue to handle ordered commit.
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
using stm::threads;
using stm::threadcount;
using stm::cohorts_node_t;

#define NOTDONE 0
#define DONE    1

#define TURBO   5
#define RESET   0
/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  NOINLINE bool validate(TxThread* tx);
  // global linklist's head
  struct cohorts_node_t* volatile q = NULL;

  struct CohortsEN2Q {
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
   *  CohortsEN2Q begin:
   *  CohortsEN2Q has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  bool
  CohortsEN2Q::begin()
  {
      TxThread* tx = stm::Self;
      tx->allocator.onTxBegin();
    S1:
      // wait until everyone is committed
      while (q != NULL);

      // before tx begins, increase total number of tx
      ADD(&started.val, 1);

      // [NB] we must double check no one is ready to commit yet
      if (q != NULL) {
          SUB(&started.val, 1);
          goto S1;
      }

      // reset tx->status;
      tx->status = RESET;

      // reset local turn val
      tx->turn.val = NOTDONE;
      return true;
  }

  /**
   *  CohortsEN2Q commit (read-only):
   */
  void
  CohortsEN2Q::commit_ro()
  {
      TxThread* tx = stm::Self;
      // decrease total number of tx started
      SUB(&started.val, 1);

      // clean up
      tx->vlist.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  CohortsEN2Q commit (in place write commit): no validation, no write back
   */
  void
  CohortsEN2Q::commit_turbo()
  {
      TxThread* tx = stm::Self;
      // decrease total number of tx started
      SUB(&started.val, 1);

      // clean up
      tx->vlist.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsEN2Q commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsEN2Q::commit_rw()
  {
      TxThread* tx = stm::Self;
      // add myself to the queue
      do {
          tx->turn.next = q;
      }while (!bcasptr(&q, tx->turn.next, &(tx->turn)));

      // decrease total number of tx started
      uint32_t temp = SUB(&started.val, 1);

      // If I'm the next to the last, notify the last txn to go turbo
      if (temp == 1)
          for (uint32_t i = 0; i < threadcount.val; i++)
              threads[i]->status = TURBO;

      // Wait for my turn
      if (tx->turn.next != NULL)
          while (tx->turn.next->val != DONE);

      // Wait until all tx are ready to commit
      while (started.val != 0);

      // Everyone must validate read
      if (!validate(tx)) {
          tx->turn.val = DONE;
          if (q == &(tx->turn)) q = NULL;
          tx->tmabort(tx);
      }

      // do write back
      tx->writes.writeback();
      CFENCE;

      // mark self done
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
   *  CohortsEN2Q read_turbo
   */
  void*
  CohortsEN2Q::read_turbo(STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsEN2Q read (read-only transaction)
   */
  void*
  CohortsEN2Q::read_ro(STM_READ_SIG(addr,))
  {
      TxThread* tx = stm::Self;
      void *tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      // test if I can go turbo
      if (tx->status == TURBO)
          GoTurbo(tx, read_turbo, write_turbo, commit_turbo);
      return tmp;
  }

  /**
   *  CohortsEN2Q read (writing transaction)
   */
  void*
  CohortsEN2Q::read_rw(STM_READ_SIG(addr,mask))
  {
      TxThread* tx = stm::Self;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      void* tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      REDO_RAW_CLEANUP(tmp, found, log, mask);
      // test if I can go turbo
      if (tx->status == TURBO) {
          tx->writes.writeback();
          GoTurbo(tx, read_turbo, write_turbo, commit_turbo);
      }
      return tmp;
  }

  /**
   *  CohortsEN2Q write (read-only context): for first write
   */
  void
  CohortsEN2Q::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      if (tx->status == TURBO) {
          // in place write
          *addr = val;
          // go turbo mode
          OnFirstWrite(tx, read_turbo, write_turbo, commit_turbo);
          return;
      }
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  CohortsEN2Q write (in place write)
   */
  void
  CohortsEN2Q::write_turbo(STM_WRITE_SIG(addr,val,mask))
  {
      *addr = val; // in place write
  }

  /**
   *  CohortsEN2Q write (writing context)
   */
  void
  CohortsEN2Q::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      if (tx->status == TURBO) {
          // write previous write set back
          foreach (WriteSet, i, tx->writes)
              *i->addr = i->val;
          CFENCE;
          // in place write
          *addr = val;
          // go turbo mode
          OnFirstWrite(tx, read_turbo, write_turbo, commit_turbo);
          return;
      }
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsEN2Q unwinder:
   */
  stm::scope_t*
  CohortsEN2Q::rollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsEN2Q in-flight irrevocability:
   */
  bool
  CohortsEN2Q::irrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsEN2Q Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsEN2Q validation for commit: check that all reads are valid
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
   *  Switch to CohortsEN2Q:
   *
   */
  void
  CohortsEN2Q::onSwitchTo()
  {
  }
}

namespace stm {
  /**
   *  CohortsEN2Q initialization
   */
  template<>
  void initTM<CohortsEN2Q>()
  {
      // set the name
      stms[CohortsEN2Q].name      = "CohortsEN2Q";
      // set the pointers
      stms[CohortsEN2Q].begin     = ::CohortsEN2Q::begin;
      stms[CohortsEN2Q].commit    = ::CohortsEN2Q::commit_ro;
      stms[CohortsEN2Q].read      = ::CohortsEN2Q::read_ro;
      stms[CohortsEN2Q].write     = ::CohortsEN2Q::write_ro;
      stms[CohortsEN2Q].rollback  = ::CohortsEN2Q::rollback;
      stms[CohortsEN2Q].irrevoc   = ::CohortsEN2Q::irrevoc;
      stms[CohortsEN2Q].switcher  = ::CohortsEN2Q::onSwitchTo;
      stms[CohortsEN2Q].privatization_safe = true;
  }
}

