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
 *  CohortsLNI2 Implementation
 *
 *  CohortsLazy with inplace write when tx is the last one in a cohort.
 */
#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  TM_FASTCALL void* CohortsLNI2ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsLNI2ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsLNI2WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsLNI2WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsLNI2CommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsLNI2CommitRW(TX_LONE_PARAMETER);

  TM_FASTCALL void CohortsLNI2CommitTurbo(TX_LONE_PARAMETER);
  TM_FASTCALL void* CohortsLNI2ReadTurbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsLNI2WriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsLNI2Validate(TxThread* tx);

  /**
   *  CohortsLNI2 begin:
   *  CohortsLNI2 has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishe their
   *  commits.
   */
  void CohortsLNI2Begin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();

    S1:
      // wait if I'm blocked
      while (gatekeeper.val == 1);

      // set started
      //
      // [mfs] Using atomicswapptr is probably optimal on x86, but probably
      // not on SPARC or ARM.  For those architectures, we probably want
      // {tx->status = COHORTS_STARTED; WBR;}
      atomicswapptr(&tx->status, COHORTS_STARTED);

      // double check no one is ready to commit
      if (gatekeeper.val == 1 || inplace.val == 1) {
          // [mfs] verify that no fences are needed here
          tx->status = COHORTS_COMMITTED;
          goto S1;
      }
  }

  /**
   *  CohortsLNI2 commit (read-only):
   */
  void CohortsLNI2CommitRO(TX_LONE_PARAMETER)
  {
      // [mfs] Do we need a read-write fence to ensure all reads are done
      //       before we write to tx->status?
      TX_GET_TX_INTERNAL;

      // mark self status
      tx->status = COHORTS_COMMITTED;

      // clean up
      tx->vlist.reset();
      OnROCommit(tx);
  }

  /**
   *  CohortsLNI2 CommitTurbo (for write in place tx use):
   */
  void CohortsLNI2CommitTurbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;

      // Mark self pending to commit
      tx->status = COHORTS_CPENDING;

      // Get order
      //
      // [mfs] I don't understand why we need this...
      tx->order = 1 + faiptr(&timestamp.val);

      // Turbo tx can clean up first
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsLNI2ReadRO, CohortsLNI2WriteRO, CohortsLNI2CommitRO);

      // Wait for my turn
      //
      // [mfs] I do not understand why this waiting is required
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // Mark self as done
      last_complete.val = tx->order;

      // I must be the last one, so release gatekeeper lock
      last_order.val = tx->order + 1;
      gatekeeper.val = 0;
      cohortcounter.val = 0;

      // Reset inplace.val write flag
      inplace.val = 0;

      // Mark self status
      tx->status = COHORTS_COMMITTED;
  }

  /**
   *  CohortsLNI2 commit (writing context):
   */
  void CohortsLNI2CommitRW(TX_LONE_PARAMETER)
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

      uint32_t left = 1;
      while (left != 0) {
          left = 0;
          for (uint32_t i = 0; i < threadcount.val; ++i)
              left += (threads[i]->status & 1);
          // if only one tx left, set global flag, inplace.val allowed
          //
          // [mfs] this is dangerous: it is possible for me to write 1, and
          //       then you to write 0 if you finish the loop first, but
          //       delay before reaching this line
          cohortcounter.val = (left == 1);
      }

      // wait for my turn to validate and do writeback
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // If I'm the first one in this cohort and no inplace.val write happened,
      // I will not do validation, else validate
      if (inplace.val == 1 || tx->order != last_order.val)
          CohortsLNI2Validate(tx);

      // Do write back
      tx->writes.writeback();

      CFENCE;

      // Mark self as done... perhaps this and self status could be combined?
      last_complete.val = tx->order;

      // Mark self status
      tx->status = COHORTS_COMMITTED;
      WBR; // this one cannot be omitted...

      // Am I the last one?
      //
      // [mfs] Can this be done without iterating through all threads?  Can
      //       we use tx->order and timestamp.val?
      for (uint32_t i = 0;lastone != false && i < threadcount.val; ++i)
          lastone &= (threads[i]->status != COHORTS_CPENDING);

      // If I'm the last one, release gatekeeper lock
      if (lastone) {
          last_order.val = tx->order + 1;
          gatekeeper.val = 0;
          cohortcounter.val = 0;
      }

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsLNI2ReadRO, CohortsLNI2WriteRO, CohortsLNI2CommitRO);
  }

  /**
   *  CohortsLNI2 read (read-only transaction)
   */
  void* CohortsLNI2ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      void* tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsLNI2 ReadTurbo (for write in place tx use)
   */
  void* CohortsLNI2ReadTurbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsLNI2 read (writing transaction)
   */
  void* CohortsLNI2ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
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
   *  CohortsLNI2 write (read-only context): for first write
   */
  void CohortsLNI2WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
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
      if (cohortcounter.val == 1) {
          // set in place write flag
          inplace.val = 1;
          // write inplace.val
          //
          // [mfs] ultimately this should use a macro that employs the mask
          *addr = val;
          // switch to turbo mode
          GoTurbo(tx, CohortsLNI2ReadTurbo, CohortsLNI2WriteTurbo, CohortsLNI2CommitTurbo);
          return;
      }

      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CohortsLNI2ReadRW, CohortsLNI2WriteRW, CohortsLNI2CommitRW);
  }
  /**
   *  CohortsLNI2 WriteTurbo: for write in place tx
   */
  void CohortsLNI2WriteTurbo(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      // [mfs] ultimately this should use a macro that employs the mask
      *addr = val;
  }

  /**
   *  CohortsLNI2 write (writing context)
   */
  void CohortsLNI2WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;

      // check if I can go turbo
      //
      // [mfs] this should be marked unlikely
      if (cohortcounter.val == 1) {
          // setup inplace write flag
          inplace.val = 1;
          // write previous write set back
          //
          // [mfs] I changed this to use the writeback(TX_LONE_PARAMETER) method, but it might
          //       have some overhead that we should avoid, depending on how
          //       it handles stack writes.
          tx->writes.writeback();

          *addr = val;
          // go turbo
          GoTurbo(tx, CohortsLNI2ReadTurbo, CohortsLNI2WriteTurbo, CohortsLNI2CommitTurbo);
          return;
      }

      // record the new value in a redo log
      //
      // [mfs] we might get better instruction scheduling if we put this code
      //       first, and then did the check.
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsLNI2 unwinder:
   */
  void CohortsLNI2Rollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsLNI2 in-flight irrevocability:
   */
  bool CohortsLNI2Irrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsLNI2 Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsLNI2 validation for commit: check that all reads are valid
   */
  void CohortsLNI2Validate(TxThread* tx)
  {
      // [mfs] this is a pretty complex loop... since it is only called once,
      //       why not have validate return a boolean, and then drop out of
      //       the cohort from the commit code.
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid) {
              // Mark self status
              tx->status = COHORTS_COMMITTED;

              // Mark self as done
              last_complete.val = tx->order;
              WBR;

              // Am I the last one?
              bool l = true;
              for (uint32_t i = 0; l != false && i < threadcount.val; ++i)
                  l &= (threads[i]->status != COHORTS_CPENDING);

              // If I'm the last one, release gatekeeper lock
              if (l) {
                  last_order.val = tx->order + 1;
                  gatekeeper.val = 0;
                  cohortcounter.val = 0;
              }
              tmabort();
          }
      }
  }

  /**
   *  Switch to CohortsLNI2:
   */
  void CohortsLNI2OnSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = timestamp.val;
      // when switching algs, mark all tx committed status
      for (uint32_t i = 0; i < threadcount.val; ++i) {
          threads[i]->status = COHORTS_COMMITTED;
      }
  }
}


DECLARE_SIMPLE_METHODS_FROM_TURBO(CohortsLNI2)
REGISTER_FGADAPT_ALG(CohortsLNI2, "CohortsLNI2", true)

#ifdef STM_ONESHOT_ALG_CohortsLNI2
DECLARE_AS_ONESHOT(CohortsLNI2)
#endif
