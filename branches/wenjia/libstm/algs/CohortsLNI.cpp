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
 *  CohortsLNI Implementation
 *
 *  CohortsLazy with inplace.val write when tx is the last one in a cohort.
 */
#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  TM_FASTCALL void* CohortsLNIReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsLNIReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsLNIWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsLNIWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsLNICommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsLNICommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsLNICommitTurbo(TX_LONE_PARAMETER);
  TM_FASTCALL void* CohortsLNIReadTurbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsLNIWriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsLNIValidate(TxThread* tx);

  /**
   *  CohortsLNI begin:
   *  CohortsLNI has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsLNIBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      //begin
      tx->allocator.onTxBegin();

    S1:
      // wait if I'm blocked
      while (gatekeeper.val == 1 || inplace.val == 1);

      // set started
      atomicswapptr(&tx->status, COHORTS_STARTED);

      // double check no one is ready to commit
      if (gatekeeper.val == 1 || inplace.val == 1){
          tx->status = COHORTS_COMMITTED;
          goto S1;
      }

      // get time of last finished txn
      tx->ts_cache = last_complete.val;
  }

  /**
   *  CohortsLNI commit (read-only):
   */
  void
  CohortsLNICommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // mark self status
      tx->status = COHORTS_COMMITTED;

      // clean up
      tx->vlist.reset();
      OnROCommit(tx);
  }

  /**
   *  CohortsLNI CommitTurbo (for write in place tx use):
   *
   */
  void
  CohortsLNICommitTurbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Mark self pending to commit
      tx->status = COHORTS_CPENDING;

      // Get order
      tx->order = 1 + faiptr(&timestamp.val);

      // Turbo tx can clean up first
      tx->vlist.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsLNIReadRO, CohortsLNIWriteRO, CohortsLNICommitRO);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // Mark self as done
      last_complete.val = tx->order;

      // I must be the last one, so release gatekeeper lock
      last_order.val = tx->order + 1;
      gatekeeper.val = 0;
      // Reset inplace.val write flag
      inplace.val = 0;

      // Mark self status
      tx->status = COHORTS_COMMITTED;
  }


  /**
   *  CohortsLNI commit (writing context):
   *
   */
  void
  CohortsLNICommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Mark a global flag, no one is allowed to begin now
      gatekeeper.val = 1;

      // Get an order
      tx->order = 1 + faiptr(&timestamp.val);

      // Mark self pending to commit
      tx->status = COHORTS_CPENDING;

      // For later use, indicates if I'm the last tx in this cohort
      bool lastone = true;

      // Wait until all tx are ready to commit
      for (uint32_t i = 0; i < threadcount.val; ++i)
          while (threads[i]->status == COHORTS_STARTED);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // If I'm the first one in this cohort and no inplace.val write happened,
      // I will do no validation, else validate
      if (inplace.val == 1 || tx->order != last_order.val)
          CohortsLNIValidate(tx);

      // Do write back
      tx->writes.writeback();

      CFENCE;
      // Mark self as done
      last_complete.val = tx->order;

      // Mark self status
      tx->status = COHORTS_COMMITTED;
      WBR;// this one cannot be omitted...

      // Am I the last one?
      for (uint32_t i = 0;lastone != false && i < threadcount.val; ++i)
          lastone &= (threads[i]->status != COHORTS_CPENDING);

      // If I'm the last one, release gatekeeper lock
      if (lastone) {
          last_order.val = tx->order + 1;
          gatekeeper.val = 0;
      }

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsLNIReadRO, CohortsLNIWriteRO, CohortsLNICommitRO);
  }

  /**
   *  CohortsLNI read (read-only transaction)
   */
  void*
  CohortsLNIReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      void *tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsLNI ReadTurbo (for write in place tx use)
   */
  void*
  CohortsLNIReadTurbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsLNI read (writing transaction)
   */
  void*
  CohortsLNIReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
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
   *  CohortsLNI write (read-only context): for first write
   */
  void
  CohortsLNIWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // [mfs] this code is not in the best location.  Consider the following
      // alternative:
      //
      // - when a thread reaches the commit function, it seals the cohort
      // - then it counts the number of transactions in the cohort
      // - then it waits for all of them to finish
      // - while waiting, it eventually knows when there is exactly one left.
      // - at that point, it can set a flag to indicate that the last one is
      //   in-flight.
      // - all transactions can check that flag on every read/write
      //
      // There are a few challenges.  First, the current code waits on the
      // first thread, then the next, then the next...  Obviously that won't do
      // anymore.  Second, there can be a "flicker" when a thread sets a flag,
      // then reads the gatekeeper, then backs out.  Lastly, RO transactions
      // will require some sort of special attention.  But the tradeoff is more
      // potential to switch (not just first write), and without so much
      // redundant checking.

      int32_t count = 0;
      // scan to check others' status
      for (uint32_t i = 0; i < threadcount.val && count < 2; ++i)
          count += (threads[i]->status == COHORTS_STARTED);

      // If every one else is ready to commit, do in place write, go turbo mode
      if (count == 1) {
          // setup in place write flag
          atomicswapptr(&inplace.val, 1);

          // double check
          for (uint32_t i = 0; i < threadcount.val; ++i)
              count -= (threads[i]->status == COHORTS_STARTED);
          if (count == 0) {
              // write inplace.val
              CohortsLNIWriteTurbo(TX_FIRST_ARG addr, val);
              // go turbo
              GoTurbo(tx, CohortsLNIReadTurbo, CohortsLNIWriteTurbo, CohortsLNICommitTurbo);
              return;
          }
          // reset flag
          inplace.val = 0;
      }
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CohortsLNIReadRW, CohortsLNIWriteRW, CohortsLNICommitRW);
  }
  /**
   *  CohortsLNI WriteTurbo: for write in place tx
   */
  void
  CohortsLNIWriteTurbo(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      *addr = val;
  }


  /**
   *  CohortsLNI write (writing context)
   */
  void
  CohortsLNIWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsLNI unwinder:
   */
  void
  CohortsLNIRollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsLNI in-flight irrevocability:
   */
  bool
  CohortsLNIIrrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsLNI Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsLNI validation for commit: check that all reads are valid
   */
  void CohortsLNIValidate(TxThread* tx)
  {
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid) {
              // Mark self status
              tx->status = COHORTS_COMMITTED;

              // Mark self as done
              last_complete.val = tx->order;
              //WBR;

              // Am I the last one?
              bool l = true;
              for (uint32_t i = 0; l != false && i < threadcount.val; ++i)
                  l &= (threads[i]->status != COHORTS_CPENDING);

              // If I'm the last one, release gatekeeper lock
              if (l) {
                  last_order.val = tx->order + 1;
                  gatekeeper.val = 0;
              }
              tmabort();
          }
      }
  }

  /**
   *  Switch to CohortsLNI:
   *
   */
  void CohortsLNIOnSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = timestamp.val;
      // when switching algs, mark all tx committed status
      for (uint32_t i = 0; i < threadcount.val; ++i) {
          threads[i]->status = COHORTS_COMMITTED;
      }
  }
}


DECLARE_SIMPLE_METHODS_FROM_TURBO(CohortsLNI)
REGISTER_FGADAPT_ALG(CohortsLNI, "CohortsLNI", true)

#ifdef STM_ONESHOT_ALG_CohortsLNI
DECLARE_AS_ONESHOT(CohortsLNI)
#endif
