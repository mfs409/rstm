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
 *  CohortsLN Implementation
 *
 *  CohortsLazy Norec Version
 */

/**
 * [mfs] see comments in lazy and norec codes elsewhere
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  // [mfs] Migrate this?
  const uintptr_t VALIDATION_FAILED = 1;

  NOINLINE uintptr_t CohortsLNValidate(TxThread* tx);
  TM_FASTCALL void* CohortsLNReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsLNReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsLNWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsLNWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsLNCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsLNCommitRW(TX_LONE_PARAMETER);

  /**
   *  CohortsLN begin:
   *  CohortsLN has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsLNBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
    S1:
      // wait if I'm blocked
      while(gatekeeper.val == 1);

      // set started
      tx->status = COHORTS_STARTED;
      WBR;

      // double check no one is ready to commit
      if (gatekeeper.val == 1){
          tx->status = COHORTS_COMMITTED;
          goto S1;
      }

      // Sample the sequence lock, if it is even decrement by 1
      tx->start_time = timestamp.val & ~(1L);

      //begin
      tx->allocator.onTxBegin();
  }

  /**
   *  CohortsLN commit (read-only):
   */
  void
  CohortsLNCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // mark self status
      tx->status = COHORTS_COMMITTED;

      // clean up
      tx->vlist.reset();
      OnROCommit(tx);
  }

  /**
   *  CohortsLN commit (writing context):
   *
   */
  void
  CohortsLNCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
    // Mark a global flag, no one is allowed to begin now
      gatekeeper.val = 1;

      // Mark self status pending to commit
      tx->status = COHORTS_CPENDING;

      // Get an order
      tx->order = 1+faiptr(&cpending.val);

      // For later use, indicates if I'm the last tx in this cohort
      bool lastone = true;

      // Wait until all tx are ready to commit
      for (uint32_t i = 0; i < threadcount.val; ++i)
          while (threads[i]->status == COHORTS_STARTED);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // get the lock and validate (use RingSTM obstruction-free
      // technique)
      while (!bcasptr(&timestamp.val, tx->start_time, tx->start_time + 1))
          if ((tx->start_time = CohortsLNValidate(tx)) == VALIDATION_FAILED) {
              // Mark self status
              tx->status = COHORTS_COMMITTED;
              WBR;
              // mark self as done
              last_complete.val = tx->order;

              // Am I the last one?
              for (uint32_t i = 0; lastone != false && i < threadcount.val; ++i)
                  lastone &= (threads[i]->status != COHORTS_CPENDING);

              // If I'm the last one, release gatekeeper.val lock
              if (lastone)
                  gatekeeper.val = 0;

              tmabort();
          }

      // do write back
      tx->writes.writeback();

      // Release the sequence lock, then clean up
      CFENCE;
      timestamp.val = tx->start_time + 2;

      // Mark self status
      tx->status = COHORTS_COMMITTED;
      WBR;

      // Mark self as done
      last_complete.val = tx->order;

      // Am I the last one?
      for (uint32_t i = 0; lastone != false && i < threadcount.val; ++i)
          lastone &= (threads[i]->status != COHORTS_CPENDING);

      // If I'm the last one, release gatekeeper.val lock
      if (lastone) {
          gatekeeper.val = 0;
      }

      // clean up
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsLNReadRO, CohortsLNWriteRO, CohortsLNCommitRO);
  }

  /**
   *  CohortsLN read (read-only transaction)
   */
  void*
  CohortsLNReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      void * tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsLN read (writing transaction)
   */
  void*
  CohortsLNReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
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
   *  CohortsLN write (read-only context): for first write
   */
  void
  CohortsLNWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CohortsLNReadRW, CohortsLNWriteRW, CohortsLNCommitRW);
  }

  /**
   *  CohortsLN write (writing context)
   */
  void
  CohortsLNWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsLN unwinder:
   */
  void
  CohortsLNRollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsLN in-flight irrevocability:
   */
  bool
  CohortsLNIrrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsLN Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsLN validation for commit: check that all reads are valid
   */
  uintptr_t
  CohortsLNValidate(TxThread* tx)
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
   *  Switch to CohortsLN:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   */
  void
  CohortsLNOnSwitchTo()
  {
      last_complete.val = 0;
      if (timestamp.val & 1)
          ++timestamp.val;
      // when switching algs, mark all tx committed status
      for (uint32_t i = 0; i < threadcount.val; ++i) {
          threads[i]->status = COHORTS_COMMITTED;
      }
  }

  /**
   *  CohortsLN initialization
   */
  template<>
  void initTM<CohortsLN>()
  {
      // set the name
      stms[CohortsLN].name      = "CohortsLN";
      // set the pointers
      stms[CohortsLN].begin     = CohortsLNBegin;
      stms[CohortsLN].commit    = CohortsLNCommitRO;
      stms[CohortsLN].read      = CohortsLNReadRO;
      stms[CohortsLN].write     = CohortsLNWriteRO;
      stms[CohortsLN].rollback  = CohortsLNRollback;
      stms[CohortsLN].irrevoc   = CohortsLNIrrevoc;
      stms[CohortsLN].switcher  = CohortsLNOnSwitchTo;
      stms[CohortsLN].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_CohortsLN
DECLARE_AS_ONESHOT_NORMAL(CohortsLN)
#endif
