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
 *  Cohorts Implementation
 *
 *  Cohorts has 4 stages. 1) Nobody is running. If anyone starts,
 *  goes to 2) Everybody is running. If anyone is ready to commit,
 *  goes to 3) Every rw tx gets an order, from now on, no one is
 *  allowed to start a tx anymore. When everyone in this cohort is
 *  ready to commit, goes to stage 4)Commit phase. Everyone commits
 *  in an order that given in stage 3. When the last one finishes
 *  its commit, it goes to stage 1. Now tx is allowed to start again.
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace stm
{
  TM_FASTCALL bool CohortsValidate(TxThread* tx);

  TM_FASTCALL void* CohortsReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsCommitRW(TX_LONE_PARAMETER);

  /**
   *  Cohorts begin:
   *  Cohorts has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
    S1:
      // wait until everyone is committed
      while (cpending.val != committed.val);

      // before tx begins, increase total number of tx
      faiptr(&started.val);

      // [NB] we must double check no one is ready to commit yet!
      if (cpending.val > committed.val) {
          faaptr(&started.val, -1);
          goto S1;
      }

      tx->allocator.onTxBegin();

      // get time of last finished txn
      tx->ts_cache = last_complete.val;
  }

  /**
   *  Cohorts commit (read-only):
   */
  void CohortsCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faaptr(&started.val, -1);

      // clean up
      tx->r_orecs.reset();
      OnROCommit(tx);
  }

  /**
   *  Cohorts commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void CohortsCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // get the order of first tx in a cohort
      uint32_t first = last_complete.val + 1;

      // increment num of tx ready to commit, and use it as the order
      tx->order = 1+faiptr(&cpending.val);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // If I'm not the first one in a cohort to commit, validate reads
      if (tx->order != (intptr_t)first)
          if (!CohortsValidate(tx)) {
              committed.val++;
              CFENCE;
              last_complete.val = tx->order;
              tmabort();
          }

      // Last one in cohort can pass the orec marking process
      //
      // [mfs] Do we use this trick in all of our algorithms?  We should!
      if ((uint32_t)tx->order != started.val) {
          // mark orec
          foreach (WriteSet, i, tx->writes) {
              // get orec
              orec_t* o = get_orec(i->addr);
              // mark orec
              o->v.all = tx->order;
          }
      }

      // Wait until all tx are ready to commit
      while (cpending.val < started.val);

      // do write back
      foreach (WriteSet, i, tx->writes)
          *i->addr = i->val;

      // increment number of committed tx
      committed.val++;
      CFENCE;

      // mark self as done
      last_complete.val = tx->order;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsReadRO, CohortsWriteRO, CohortsCommitRO);
  }

  /**
   *  Cohorts read (read-only transaction)
   */
  void* CohortsReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // log orec
      tx->r_orecs.insert(get_orec(addr));
      return *addr;
  }

  /**
   *  Cohorts read (writing transaction)
   */
  void* CohortsReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // log orec
      tx->r_orecs.insert(get_orec(addr));

      void* tmp = *addr;
      REDO_RAW_CLEANUP(tmp, found, log, mask);
      return tmp;
  }

  /**
   *  Cohorts write (read-only context): for first write
   */
  void CohortsWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CohortsReadRW, CohortsWriteRW, CohortsCommitRW);
  }

  /**
   *  Cohorts write (writing context)
   */
  void CohortsWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  Cohorts unwinder:
   */
  void CohortsRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();

      PostRollback(tx);
  }

  /**
   *  Cohorts in-flight irrevocability:
   */
  bool CohortsIrrevoc(TxThread*)
  {
      UNRECOVERABLE("Cohorts Irrevocability not yet supported");
      return false;
  }

  bool CohortsValidate(TxThread* tx)
  {
      // [mfs] use the luke trick?
      foreach (OrecList, i, tx->r_orecs) {
          // If orec changed, abort
          if ((*i)->v.all > tx->ts_cache)
              return false;
      }
      return true;
  }

  /**
   *  Switch to Cohorts:
   *
   */
  void CohortsOnSwitchTo()
  {
      last_complete.val = 0;
  }

  /**
   *  Cohorts initialization
   */
  template<>
  void initTM<Cohorts>()
  {
      // set the name
      stms[Cohorts].name      = "Cohorts";
      // set the pointers
      stms[Cohorts].begin     = CohortsBegin;
      stms[Cohorts].commit    = CohortsCommitRO;
      stms[Cohorts].read      = CohortsReadRO;
      stms[Cohorts].write     = CohortsWriteRO;
      stms[Cohorts].rollback  = CohortsRollback;
      stms[Cohorts].irrevoc   = CohortsIrrevoc;
      stms[Cohorts].switcher  = CohortsOnSwitchTo;
      stms[Cohorts].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_Cohorts
DECLARE_AS_ONESHOT_NORMAL(Cohorts)
#endif
