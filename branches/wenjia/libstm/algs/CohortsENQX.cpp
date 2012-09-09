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
 *  CohortsENQX Implementation
 *
 *  CohortsENQX is CohortsNorec with inplace write if I'm the last one in the
 *  cohort. Use queue to handle ordered commit.
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  TM_FASTCALL bool CohortsENQXValidate(TxThread* tx);
  TM_FASTCALL void* CohortsENQXReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsENQXReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsENQXReadTurbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsENQXWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsENQXWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsENQXWriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsENQXCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsENQXCommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsENQXCommitTurbo(TX_LONE_PARAMETER);


  /**
   *  CohortsENQX begin:
   *  CohortsENQX has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsENQXBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
    S1:
      // wait until everyone is committed
      while (q != NULL || inplace.val == 1 || sealed.val == 1);

      // before tx begins, increase total number of tx
      faiptr(&started.val);

      // [NB] we must double check no one is ready to commit yet
      // and no one entered in place write phase(turbo mode)
      if (q != NULL|| inplace.val == 1 || sealed.val == 1){
          faaptr(&started.val, -1);
          goto S1;
      }

      // reset threadlocal variables
      tx->turn.val = COHORTS_NOTDONE;
      tx->cohort_writes = 0;
      tx->cohort_reads = 0;

      // reset local turn val
      tx->turn.val = COHORTS_NOTDONE;
  }

  /**
   *  CohortsENQX commit (read-only):
   */
  void
  CohortsENQXCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faaptr(&started.val, -1);

      // clean up
      tx->vlist.reset();
      OnROCommit(tx);
  }

  /**
   *  CohortsENQX commit (in place write commit): no validation, no write back
   *  no other thread touches cpending
   */
  void
  CohortsENQXCommitTurbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faaptr(&started.val, -1);

      // clean up
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsENQXReadRO, CohortsENQXWriteRO, CohortsENQXCommitRO);

      // wait for tx in CommitRW finish
      while (q != NULL);

      // reset in place write flag
      sealed.val = 0;
      inplace.val = 0;
  }

  /**
   *  CohortsENQX commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsENQXCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // add myself to the queue
      do {
          tx->turn.next = q;
      }while (!bcasptr(&q, tx->turn.next, &(tx->turn)));

      // decrease total number of tx started
      faaptr(&started.val, -1);

      // wait for my turn
      if (tx->turn.next != NULL)
          while (tx->turn.next->val != COHORTS_DONE);

      // Wait until all tx are ready to commit
      while (started.val != 0);

      // If in place write occurred, all tx validate reads
      // Otherwise, only first one skips validation
      if (inplace.val == 1 || tx->turn.next != NULL)
          if (!CohortsENQXValidate(tx)) {
              // mark self done
              tx->turn.val = COHORTS_DONE;
              // reset q if last one
              if (q == &(tx->turn)) {
                  sealed.val = 0;
                  q = NULL;
              }
              // abort
              tmabort();
          }

      // do write back
      tx->writes.writeback();
      CFENCE;

      // mark self as done
      tx->turn.val = COHORTS_DONE;

      // last one in cohort reset q
      if (q == &(tx->turn)) {
          sealed.val = 0;
          q = NULL;
      }

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsENQXReadRO, CohortsENQXWriteRO, CohortsENQXCommitRO);
  }

  /**
   *  CohortsENQX ReadTurbo
   */
  void*
  CohortsENQXReadTurbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsENQX read (read-only transaction)
   */
  void*
  CohortsENQXReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;

      tx->cohort_reads++;

      void *tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsENQX read (writing transaction)
   */
  void*
  CohortsENQXReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // test if we need to do a early seal based on write number
      if (tx->cohort_reads == READ_EARLYSEAL.val)
          atomicswap32(&sealed.val, 1);
      tx->cohort_reads++;

      void* tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      REDO_RAW_CLEANUP(tmp, found, log, mask);
      return tmp;
  }

  /**
   *  CohortsENQX write (read-only context): for first write
   */
  void
  CohortsENQXWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // If everyone else is ready to commit, do in place write
      if (started.val == 1) {
          // set up flag indicating in place write starts
          atomicswapptr(&inplace.val, 1);
          // double check is necessary
          if (started.val == 1) {
              // in place write
              *addr = val;
              // go turbo mode
              GoTurbo(tx, CohortsENQXReadTurbo, CohortsENQXWriteTurbo, CohortsENQXCommitTurbo);
              return;
          }
          // reset flag
          inplace.val = 0;
      }
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CohortsENQXReadRW, CohortsENQXWriteRW, CohortsENQXCommitRW);
  }

  /**
   *  CohortsENQX write (in place write)
   */
  void
  CohortsENQXWriteTurbo(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      *addr = val; // in place write
  }

  /**
   *  CohortsENQX write (writing context)
   */
  void
  CohortsENQXWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsENQX unwinder:
   */
  void
  CohortsENQXRollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsENQX in-flight irrevocability:
   */
  bool
  CohortsENQXIrrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsENQX Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsENQX validation for commit: check that all reads are valid
   */
  bool
  CohortsENQXValidate(TxThread* tx)
  {
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid) return false;
      }
      return true;
  }

  /**
   *  Switch to CohortsENQX:
   *
   */
  void
  CohortsENQXOnSwitchTo()
  {
      inplace.val = 0;
      sealed.val = 0;
      //[wer210] get a configuration for CohortsLNIQX
      // write
      const char* cfgwrites = "-1";
      const char* cfgstring1 = getenv("STM_WRITES");
      if (cfgstring1)
          cfgwrites = cfgstring1;

      switch (*cfgwrites) {
        case '-': WRITE_EARLYSEAL.val = -1; break;
        case '0': WRITE_EARLYSEAL.val = 0; break;
        case '1': WRITE_EARLYSEAL.val = 1; break;
        case '2': WRITE_EARLYSEAL.val = 2; break;
        case '3': WRITE_EARLYSEAL.val = 3;
      };

      // read
      const char* cfgreads = "-1";
      const char* cfgstring2 = getenv("STM_READS");
      if (cfgstring2)
          cfgreads = cfgstring2;

      switch (*cfgreads) {
        case '-': READ_EARLYSEAL.val = -1; break;
        case '0': READ_EARLYSEAL.val = 0; break;
        case '1': READ_EARLYSEAL.val = 1; break;
        case '2': READ_EARLYSEAL.val = 2; break;
        case '3': READ_EARLYSEAL.val = 3;
      };

      // abort
      const char* cfgaborts = "-1";
      const char* cfgstring3 = getenv("STM_ABORTS");
      if (cfgstring3)
          cfgaborts = cfgstring3;

      switch (*cfgaborts) {
        case '-': ABORT_EARLYSEAL.val = -1; break;
        case '0': ABORT_EARLYSEAL.val = 0; break;
        case '1': ABORT_EARLYSEAL.val = 1; break;
        case '2': ABORT_EARLYSEAL.val = 2; break;
        case '3': ABORT_EARLYSEAL.val = 3;
      };
      //      printf("Use STM_READS = %d, STM_WRITES = %d, STM_ABORTS = %d\n",
      //READ_EARLYSEAL.val, WRITE_EARLYSEAL.val, ABORT_EARLYSEAL.val);
  }
}


DECLARE_SIMPLE_METHODS_FROM_TURBO(CohortsENQX)
REGISTER_FGADAPT_ALG(CohortsENQX, "CohortsENQX", true)

#ifdef STM_ONESHOT_ALG_CohortsENQX
DECLARE_AS_ONESHOT(CohortsENQX)
#endif
