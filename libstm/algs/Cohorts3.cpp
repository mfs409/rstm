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
 *  Cohorts3 Implementation
 *
 *  CohortsNOrec with a queue to handle order
 */

#include "../profiling.hpp"
#include "../algs.hpp"
#include "../RedoRAWUtils.hpp"

// define atomic operations
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
  // global linklist's head
  struct cohorts_node_t* volatile q = NULL;
  NOINLINE bool validate(TxThread* tx);

  struct Cohorts3 {
      static void begin();
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
   *  Cohorts3 begin:
   *  Cohorts3 has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void Cohorts3::begin()
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

      // reset local turn val
      tx->turn.val = NOTDONE;
  }

  /**
   *  Cohorts3 commit (read-only):
   */
  void
  Cohorts3::commit_ro()
  {
      TxThread* tx = stm::Self;
      // decrease total number of tx started
      SUB(&started.val, 1);

      // clean up
      tx->vlist.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  Cohorts3 commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  Cohorts3::commit_rw()
  {
      TxThread* tx = stm::Self;
      // add myself to the queue
      do{
          tx->turn.next = q;
      } while (!bcasptr(&q, tx->turn.next, &(tx->turn)));

      // decrease total number of tx started
      SUB(&started.val, 1);

      // if I'm not the 1st one in cohort
      if (tx->turn.next != NULL) {
          // wait for my turn
          while (tx->turn.next->val != DONE);
          // validate reads
          if (!validate(tx)) {
              // mark self done
              tx->turn.val = DONE;
              // reset q if last one
              if (q == &(tx->turn)) q = NULL;
              // abort
              tx->tmabort();
          }
      }

      // Wait until all tx are ready to commit
      while (started.val != 0);

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
   *  Cohorts3 read (read-only transaction)
   */
  void*
  Cohorts3::read_ro(STM_READ_SIG(addr,))
  {
      TxThread* tx = stm::Self;
      void * tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  Cohorts3 read (writing transaction)
   */
  void*
  Cohorts3::read_rw(STM_READ_SIG(addr,mask))
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
   *  Cohorts3 write (read-only context): for first write
   */
  void
  Cohorts3::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  Cohorts3 write (writing context)
   */
  void
  Cohorts3::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  Cohorts3 unwinder:
   */
  void
  Cohorts3::rollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  Cohorts3 in-flight irrevocability:
   */
  bool
  Cohorts3::irrevoc(TxThread*)
  {
      UNRECOVERABLE("Cohorts3 Irrevocability not yet supported");
      return false;
  }

  /**
   *  Cohorts3 validation for commit
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
   *  Switch to Cohorts3:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   */
  void
  Cohorts3::onSwitchTo()
  {
  }
}

namespace stm {
  /**
   *  Cohorts3 initialization
   */
  template<>
  void initTM<Cohorts3>()
  {
      // set the name
      stms[Cohorts3].name      = "Cohorts3";
      // set the pointers
      stms[Cohorts3].begin     = ::Cohorts3::begin;
      stms[Cohorts3].commit    = ::Cohorts3::commit_ro;
      stms[Cohorts3].read      = ::Cohorts3::read_ro;
      stms[Cohorts3].write     = ::Cohorts3::write_ro;
      stms[Cohorts3].rollback  = ::Cohorts3::rollback;
      stms[Cohorts3].irrevoc   = ::Cohorts3::irrevoc;
      stms[Cohorts3].switcher  = ::Cohorts3::onSwitchTo;
      stms[Cohorts3].privatization_safe = true;
  }
}
