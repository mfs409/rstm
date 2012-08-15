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

#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  TM_FASTCALL bool CohortsENQValidate(TxThread* tx);
  TM_FASTCALL void* CohortsENQReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsENQReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsENQReadTurbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsENQWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsENQWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsENQWriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsENQCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsENQCommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsENQCommitTurbo(TX_LONE_PARAMETER);


  /**
   *  CohortsENQ begin:
   *  CohortsENQ has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsENQBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
    S1:
      // wait until everyone is committed
      while (q != NULL);

      // before tx begins, increase total number of tx
      faiptr(&started.val);

      // [NB] we must double check no one is ready to commit yet
      // and no one entered in place write phase(turbo mode)
      if (q != NULL|| inplace.val == 1){
          faaptr(&started.val, -1);
          goto S1;
      }

      // reset local turn val
      tx->turn.val = COHORTS_NOTDONE;
  }

  /**
   *  CohortsENQ commit (read-only):
   */
  void
  CohortsENQCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faaptr(&started.val, -1);

      // clean up
      tx->vlist.reset();
      OnROCommit(tx);
  }

  /**
   *  CohortsENQ commit (in place write commit): no validation, no write back
   *  no other thread touches cpending
   */
  void
  CohortsENQCommitTurbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faaptr(&started.val, -1);

      // clean up
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsENQReadRO, CohortsENQWriteRO, CohortsENQCommitRO);

      // wait for tx in CommitRW finish
      while (q != NULL);

      // reset in place write flag
      inplace.val = 0;
  }

  /**
   *  CohortsENQ commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsENQCommitRW(TX_LONE_PARAMETER)
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
          if (!CohortsENQValidate(tx)) {
              // mark self done
              tx->turn.val = COHORTS_DONE;
              // reset q if last one
              if (q == &(tx->turn)) q = NULL;
              // abort
              tmabort();
          }

      // do write back
      tx->writes.writeback();
      CFENCE;

      // mark self as done
      tx->turn.val = COHORTS_DONE;

      // last one in cohort reset q
      if (q == &(tx->turn))
          q = NULL;

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsENQReadRO, CohortsENQWriteRO, CohortsENQCommitRO);
  }

  /**
   *  CohortsENQ ReadTurbo
   */
  void*
  CohortsENQReadTurbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsENQ read (read-only transaction)
   */
  void*
  CohortsENQReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      void *tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsENQ read (writing transaction)
   */
  void*
  CohortsENQReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
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
   *  CohortsENQ write (read-only context): for first write
   */
  void
  CohortsENQWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
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
              OnFirstWrite(tx, CohortsENQReadTurbo, CohortsENQWriteTurbo, CohortsENQCommitTurbo);
              return;
          }
          // reset flag
          inplace.val = 0;
      }
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CohortsENQReadRW, CohortsENQWriteRW, CohortsENQCommitRW);
  }

  /**
   *  CohortsENQ write (in place write)
   */
  void
  CohortsENQWriteTurbo(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      *addr = val; // in place write
  }

  /**
   *  CohortsENQ write (writing context)
   */
  void
  CohortsENQWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsENQ unwinder:
   */
  void
  CohortsENQRollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsENQ in-flight irrevocability:
   */
  bool
  CohortsENQIrrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsENQ Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsENQ validation for commit: check that all reads are valid
   */
  bool
  CohortsENQValidate(TxThread* tx)
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
  CohortsENQOnSwitchTo()
  {
      inplace.val = 0;
  }

  /**
   *  CohortsENQ initialization
   */
  template<>
  void initTM<CohortsENQ>()
  {
      // set the name
      stms[CohortsENQ].name      = "CohortsENQ";
      // set the pointers
      stms[CohortsENQ].begin     = CohortsENQBegin;
      stms[CohortsENQ].commit    = CohortsENQCommitRO;
      stms[CohortsENQ].read      = CohortsENQReadRO;
      stms[CohortsENQ].write     = CohortsENQWriteRO;
      stms[CohortsENQ].rollback  = CohortsENQRollback;
      stms[CohortsENQ].irrevoc   = CohortsENQIrrevoc;
      stms[CohortsENQ].switcher  = CohortsENQOnSwitchTo;
      stms[CohortsENQ].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_CohortsENQ
DECLARE_AS_ONESHOT_TURBO(CohortsENQ)
#endif
