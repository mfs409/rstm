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
 *  CohortsEager Implementation
 *
 *  Similiar to Cohorts, except that if I'm the last one in the cohort, I
 *  go to turbo mode, do in place read and write, and do turbo commit.
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace stm
{
  TM_FASTCALL void* CohortsEagerReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsEagerReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsEagerReadTurbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsEagerWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsEagerWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsEagerWriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsEagerCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsEagerCommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsEagerCommitTurbo(TX_LONE_PARAMETER);
  NOINLINE void CohortsEagerValidate(TxThread* tx);

  /**
   *  CohortsEager begin:
   *  CohortsEager has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsEagerBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();

    S1:
      // wait until everyone is committed
      while (cpending.val != committed.val);

      // before tx begins, increase total number of tx
      faiptr(&started.val);

      // [NB] we must double check no one is ready to commit yet
      // and no one entered in place write phase(turbo mode)
      if (cpending.val > committed.val || inplace == 1){
          faaptr(&started.val, -1);
          goto S1;
      }

      // get time of last finished txn
      tx->ts_cache = last_complete.val;
  }

  /**
   *  CohortsEager commit (read-only):
   */
  void
  CohortsEagerCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faaptr(&started.val, -1);

      // clean up
      tx->r_orecs.reset();
      OnROCommit(tx);
  }

  /**
   *  CohortsEager commit (in place write commit): no validation, no write back
   *  no other thread touches cpending
   */
  void
  CohortsEagerCommitTurbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // increase # of tx waiting to commit, and use it as the order
      tx->order = 1 + faiptr(&cpending.val);

      // clean up
      tx->r_orecs.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsEagerReadRO, CohortsEagerWriteRO, CohortsEagerCommitRO);

      // wait for my turn, in this case, cpending is my order
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // reset in place write flag
      inplace = 0;

      // increase total number of tx committed
      committed.val++;

      // mark self as done
      last_complete.val = tx->order;
  }

  /**
   *  CohortsEager commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsEagerCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // increase # of tx waiting to commit, and use it as the order
      tx->order = 1 + faiptr(&cpending.val);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // Wait until all tx are ready to commit
      while (cpending.val < started.val);

      // If in place write occurred, all tx validate reads
      // Otherwise, only first one skips validation
      if (inplace == 1 || tx->order != last_order)
          CohortsEagerValidate(tx);

      // Last one doesn't needs to mark orec
      if ((uint32_t)tx->order != started.val)
          foreach (WriteSet, i, tx->writes) {
              // get orec
              orec_t* o = get_orec(i->addr);
              // mark orec
              o->v.all = tx->order;
              // do write back
              *i->addr = i->val;
          }
      else
          tx->writes.writeback();

      // update last_order
      last_order = started.val + 1;

      // increase total tx committed
      committed.val++;
      CFENCE;

      // mark self as done
      last_complete.val = tx->order;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsEagerReadRO, CohortsEagerWriteRO, CohortsEagerCommitRO);
  }

  /**
   *  CohortsEager ReadTurbo
   */
  void*
  CohortsEagerReadTurbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsEager read (read-only transaction)
   */
  void*
  CohortsEagerReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // log orec
      tx->r_orecs.insert(get_orec(addr));
      return *addr;
  }

  /**
   *  CohortsEager read (writing transaction)
   */
  void*
  CohortsEagerReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
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
   *  CohortsEager write (read-only context): for first write
   */
  void
  CohortsEagerWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // If everyone else is ready to commit, do in place write
      if (cpending.val + 1 == started.val) {
          // set up flag indicating in place write starts
          inplace = 1;
          WBR;
          // double check is necessary
          if (cpending.val + 1 == started.val) {
              // get my order;
              tx->order = cpending.val + 1;
              CFENCE;
              // mark orec
              orec_t* o = get_orec(addr);
              o->v.all = tx->order;
              // in place write
              *addr = val;
              // go turbo mode
              OnFirstWrite(tx, CohortsEagerReadTurbo, CohortsEagerWriteTurbo, CohortsEagerCommitTurbo);
              return;
          }
          // reset flag
          inplace = 0;
      }
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CohortsEagerReadRW, CohortsEagerWriteRW, CohortsEagerCommitRW);
  }

  /**
   *  CohortsEager write (in place write)
   */
  void
  CohortsEagerWriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      orec_t* o = get_orec(addr);
      o->v.all = tx->order; // mark orec
      *addr = val; // in place write
  }

  /**
   *  CohortsEager write (writing context)
   */
  void
  CohortsEagerWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsEager unwinder:
   */
  void
  CohortsEagerRollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsEager in-flight irrevocability:
   */
  bool
  CohortsEagerIrrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsEager Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsEager validation for commit: check that all reads are valid
   */
  void
  CohortsEagerValidate(TxThread* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // If orec changed , abort
          if (ivt > tx->ts_cache) {
              // increase total number of committed tx
              // ADD(&committed.val, 1);
              committed.val ++;
              CFENCE;
              // set self as completed
              last_complete.val = tx->order;
              // abort
              tmabort();
          }
      }
  }

  /**
   *  Switch to CohortsEager:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   */
  void
  CohortsEagerOnSwitchTo()
  {
      last_complete.val = 0;
      inplace = 0;
  }

  /**
   *  CohortsEager initialization
   */
  template<>
  void initTM<CohortsEager>()
  {
      // set the name
      stms[CohortsEager].name      = "CohortsEager";
      // set the pointers
      stms[CohortsEager].begin     = CohortsEagerBegin;
      stms[CohortsEager].commit    = CohortsEagerCommitRO;
      stms[CohortsEager].read      = CohortsEagerReadRO;
      stms[CohortsEager].write     = CohortsEagerWriteRO;
      stms[CohortsEager].rollback  = CohortsEagerRollback;
      stms[CohortsEager].irrevoc   = CohortsEagerIrrevoc;
      stms[CohortsEager].switcher  = CohortsEagerOnSwitchTo;
      stms[CohortsEager].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_CohortsEager
DECLARE_AS_ONESHOT_TURBO(CohortsEager)
#endif
