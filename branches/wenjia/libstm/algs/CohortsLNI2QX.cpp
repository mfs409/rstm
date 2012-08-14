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
 *  CohortsLNI2QX Implementation
 *
 *  CohortsLazy with inplace write when tx is the last one in a cohort.
 *  Early Seal CohortsLNI2Q
 */
#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  NOINLINE bool CohortsLNI2QXValidate(TxThread* tx);
  TM_FASTCALL void* CohortsLNI2QXReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsLNI2QXReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsLNI2QXWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsLNI2QXWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsLNI2QXCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsLNI2QXCommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsLNI2QXCommitTurbo(TX_LONE_PARAMETER);
  TM_FASTCALL void* CohortsLNI2QXReadTurbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsLNI2QXWriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));

  /**
   *  CohortsLNI2QX begin:
   *  CohortsLNI2QX has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishe their
   *  commits.
   */
  void CohortsLNI2QXBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
    S1:
      // wait if I'm blocked
      while (q != NULL || sealed.val == 1);

      // set started
      tx->status = COHORTS_STARTED;
      WBR;

      // double check no one is ready to commit
      if (q != NULL || sealed.val == 1) {
          // [mfs] verify that no fences are needed here
          // [wer210] I don't think we need fence here...
          //          although verified by only testing benches
          tx->status = COHORTS_COMMITTED;
          goto S1;
      }
      // reset threadlocal variables
      tx->turn.val = COHORTS_NOTDONE;
      tx->cohort_writes = 0;
      tx->cohort_reads = 0;

      // test if we need to do a early seal based on abort number
      if (tx->cohort_aborts == ABORT_EARLYSEAL) {
          atomicswap32(&sealed.val, 1);
          tx->cohort_aborts = 0;
      }
  }

  /**
   *  CohortsLNI2QX commit (read-only):
   */
  void CohortsLNI2QXCommitRO(TX_LONE_PARAMETER)
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
   *  CohortsLNI2QX CommitTurbo (for write inplace tx use):
   */
  void CohortsLNI2QXCommitTurbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Mark self committed
      tx->status = COHORTS_COMMITTED;

      // clean up
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsLNI2QXReadRO, CohortsLNI2QXWriteRO, CohortsLNI2QXCommitRO);
  }

  /**
   *  CohortsLNI2QX commit (writing context):
   */
  void CohortsLNI2QXCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // pointer to the predecessor node in the queue
      struct cohorts_node_t* pred;

      // add myself to the queue
      //do {
      //    tx->turn.next = q;
      //} while (!bcasptr(&q, tx->turn.next, &(tx->turn)));
      pred = __sync_lock_test_and_set(&q, &(tx->turn));

      // Mark self pending to commit
      tx->status = COHORTS_CPENDING;
      WBR;

      // if only one tx left, set global flag, inplace allowed
      uint32_t left = 0;
      for (uint32_t i = 0; i < threadcount.val; ++i)
          left += (threads[i]->status & 1);
      //
      // [mfs] this is dangerous: it is possible for me to write 1, and
      //       then you to write 0 if you finish the loop first, but
      //       delay before reaching this line
      // [wer210] it doesn't matter cuz write 0 is not dangerous,
      //           just forbit one possible inplace write.
      cohortcounter.val = (left == 1);

      /*
      // Not first one? wait for your turn
      if (tx->turn.next != NULL)
          while (tx->turn.next->val != COHORTS_DONE);
      else {
          // First one in a cohort waits until all tx are ready to commit
          for (uint32_t i = 0; i < threadcount.val; ++i)
              while (threads[i]->status == COHORTS_STARTED);
      }
      */
      if (pred != NULL)
          while (pred->val != COHORTS_DONE);
      else {
          // First one in a cohort waits until all tx are ready to commit
          for (uint32_t i = 0; i < threadcount.val; ++i)
              while (threads[i]->status == COHORTS_STARTED);
      }

      // Everyone must validate read
      if (!CohortsLNI2QXValidate(tx)) {
          // count the number of aborts
          tx->cohort_aborts ++;
          // mark self done
          tx->turn.val = COHORTS_DONE;
          if (q == &(tx->turn)) {
              cohortcounter.val = 0;
              CFENCE;
              sealed.val = 0;
              q = NULL;
          }
          tmabort();
      }

      // Do write back
      tx->writes.writeback();
      CFENCE;

      // Mark self status
      tx->turn.val = COHORTS_DONE;

      // last one in a cohort reset q
      if (q == &(tx->turn)) {
          cohortcounter.val = 0;
          CFENCE;
          sealed.val = 0;
          q = NULL;
      }

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsLNI2QXReadRO, CohortsLNI2QXWriteRO, CohortsLNI2QXCommitRO);
  }

  /**
   *  CohortsLNI2QX read (read-only transaction)
   */
  void* CohortsLNI2QXReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;

      tx->cohort_reads++;
      // test if we need to do a early seal based on write number
      if (tx->cohort_reads == READ_EARLYSEAL)
          atomicswap32(&sealed.val, 1);

      void* tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsLNI2QX ReadTurbo (for write in place tx use)
   */
  void* CohortsLNI2QXReadTurbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsLNI2QX read (writing transaction)
   */
  void* CohortsLNI2QXReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
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
   *  CohortsLNI2QX write (read-only context): for first write
   */
  void CohortsLNI2QXWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      if (cohortcounter.val == 1) {
          *addr = val;
          // switch to turbo mode
          OnFirstWrite(tx, CohortsLNI2QXReadTurbo, CohortsLNI2QXWriteTurbo, CohortsLNI2QXCommitTurbo);
          return;
      }

      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CohortsLNI2QXReadRW, CohortsLNI2QXWriteRW, CohortsLNI2QXCommitRW);
  }
  /**
   *  CohortsLNI2QX WriteTurbo: for write in place tx
   */
  void CohortsLNI2QXWriteTurbo(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      // [mfs] ultimately this should use a macro that employs the mask
      *addr = val;
  }

  /**
   *  CohortsLNI2QX write (writing context)
   */
  void CohortsLNI2QXWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;

      // record the new value in a redo log
      //
      // [mfs] we might get better instruction scheduling if we put this code
      //       first, and then did the check.
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      tx->cohort_writes++;
      // test if we need to do a early seal based on write number
      if (tx->cohort_writes == WRITE_EARLYSEAL)
          atomicswap32(&sealed.val, 1);

      // check if I can go turbo
      //
      // [mfs] this should be marked unlikely
      if (cohortcounter.val == 1) {
          // [mfs] I changed this to use the writeback(TX_LONE_PARAMETER) method, but it might
          //       have some overhead that we should avoid, depending on how
          //       it handles stack writes.
          tx->writes.writeback();
          *addr = val;
          // go turbo
          GoTurbo(tx, CohortsLNI2QXReadTurbo, CohortsLNI2QXWriteTurbo, CohortsLNI2QXCommitTurbo);
      }
  }

  /**
   *  CohortsLNI2QX unwinder:
   */
  void CohortsLNI2QXrollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsLNI2QX in-flight irrevocability:
   */
  bool CohortsLNI2QXirrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsLNI2QX Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsLNI2QX validation for commit: check that all reads are valid
   */
  bool
  CohortsLNI2QXValidate(TxThread* tx)
  {
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid) return false;
      }
      return true;
  }

  /**
   *  Switch to CohortsLNI2QX:
   */
  void CohortsLNI2QXonSwitchTo()
  {
      // when switching algs, mark all tx committed status
      for (uint32_t i = 0; i < threadcount.val; ++i) {
          threads[i]->status = COHORTS_COMMITTED;
      }

      //[wer210] get a configuration for CohortsLNI2QX
      // write
      const char* cfgwrites = "-1";
      const char* cfgstring1 = getenv("STM_WRITES");
      if (cfgstring1)
          cfgwrites = cfgstring1;

      switch (*cfgwrites) {
        case '-': WRITE_EARLYSEAL = -1; break;
        case '0': WRITE_EARLYSEAL = 0; break;
        case '1': WRITE_EARLYSEAL = 1; break;
        case '2': WRITE_EARLYSEAL = 2; break;
        case '3': WRITE_EARLYSEAL = 3;
      };

      // read
      const char* cfgreads = "-1";
      const char* cfgstring2 = getenv("STM_READS");
      if (cfgstring2)
          cfgreads = cfgstring2;

      switch (*cfgreads) {
        case '-': READ_EARLYSEAL = -1; break;
        case '0': READ_EARLYSEAL = 0; break;
        case '1': READ_EARLYSEAL = 1; break;
        case '2': READ_EARLYSEAL = 2; break;
        case '3': READ_EARLYSEAL = 3;
      };

      // abort
      const char* cfgaborts = "-1";
      const char* cfgstring3 = getenv("STM_ABORTS");
      if (cfgstring3)
          cfgaborts = cfgstring3;

      switch (*cfgaborts) {
        case '-': ABORT_EARLYSEAL = -1; break;
        case '0': ABORT_EARLYSEAL = 0; break;
        case '1': ABORT_EARLYSEAL = 1; break;
        case '2': ABORT_EARLYSEAL = 2; break;
        case '3': ABORT_EARLYSEAL = 3;
      };
      printf("Use STM_READS = %d, STM_WRITES = %d, STM_ABORTS = %d\n",
             READ_EARLYSEAL, WRITE_EARLYSEAL, ABORT_EARLYSEAL);
  }

  /**
   *  CohortsLNI2QX initialization
   */
  template<>
  void initTM<CohortsLNI2QX>()
  {
      // set the name
      stms[CohortsLNI2QX].name      = "CohortsLNI2QX";
      // set the pointers
      stms[CohortsLNI2QX].begin     = CohortsLNI2QXBegin;
      stms[CohortsLNI2QX].commit    = CohortsLNI2QXCommitRO;
      stms[CohortsLNI2QX].read      = CohortsLNI2QXReadRO;
      stms[CohortsLNI2QX].write     = CohortsLNI2QXWriteRO;
      stms[CohortsLNI2QX].rollback  = CohortsLNI2QXrollback;
      stms[CohortsLNI2QX].irrevoc   = CohortsLNI2QXirrevoc;
      stms[CohortsLNI2QX].switcher  = CohortsLNI2QXonSwitchTo;
      stms[CohortsLNI2QX].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_CohortsLNI2QX
DECLARE_AS_ONESHOT_TURBO(CohortsLNI2QX)
#endif
