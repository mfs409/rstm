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
 *  CohortsQ Implementation
 *
 *  CohortsNOrec with a queue to handle order
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  TM_FASTCALL bool CohortsQValidate(TxThread* tx);
  TM_FASTCALL void* CohortsQReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsQReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsQWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsQWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsQCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsQCommitRW(TX_LONE_PARAMETER);

  /**
   *  CohortsQ begin:
   *  CohortsQ has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsQBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();

    S1:
      // wait until everyone is committed
      while (q != NULL);

      // before tx begins, increase total number of tx
      faiptr(&started.val);

      // [NB] we must double check no one is ready to commit yet
      if (q != NULL) {
          faaptr(&started.val, -1);
          goto S1;
      }

      // reset local turn val
      tx->turn.val = COHORTS_NOTDONE;
  }

  /**
   *  CohortsQ commit (read-only):
   */
  void
  CohortsQCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faaptr(&started.val, -1);

      // clean up
      tx->vlist.reset();
      OnROCommit(tx);
  }

  /**
   *  CohortsQ commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsQCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // add myself to the queue
      do{
          tx->turn.next = q;
      } while (!bcasptr(&q, tx->turn.next, &(tx->turn)));

      // decrease total number of tx started
      faaptr(&started.val, -1);

      // if I'm not the 1st one in cohort
      if (tx->turn.next != NULL) {
          // wait for my turn
          while (tx->turn.next->val != COHORTS_DONE);
          // validate reads
          if (!CohortsQValidate(tx)) {
              // mark self done
              tx->turn.val = COHORTS_DONE;
              // reset q if last one
              if (q == &(tx->turn)) q = NULL;
              // abort
              tmabort();
          }
      }

      // Wait until all tx are ready to commit
      while (started.val != 0);

      // do write back
      tx->writes.writeback();
      CFENCE;

      // mark self done
      tx->turn.val = COHORTS_DONE;

      // last one in cohort reset q
      if (q == &(tx->turn))
          q = NULL;

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsQReadRO, CohortsQWriteRO, CohortsQCommitRO);
  }

  /**
   *  CohortsQ read (read-only transaction)
   */
  void*
  CohortsQReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      void * tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsQ read (writing transaction)
   */
  void*
  CohortsQReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
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
   *  CohortsQ write (read-only context): for first write
   */
  void
  CohortsQWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CohortsQReadRW, CohortsQWriteRW, CohortsQCommitRW);
  }

  /**
   *  CohortsQ write (writing context)
   */
  void
  CohortsQWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsQ unwinder:
   */
  void
  CohortsQRollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsQ in-flight irrevocability:
   */
  bool
  CohortsQIrrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsQ Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsQ validation for commit
   */
  bool
  CohortsQValidate(TxThread* tx)
  {
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid) return false;
      }
      return true;
  }


  /**
   *  Switch to CohortsQ:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   */
  void
  CohortsQOnSwitchTo()
  {
  }
}

DECLARE_SIMPLE_METHODS_FROM_NORMAL(CohortsQ)
REGISTER_FGADAPT_ALG(CohortsQ, "CohortsQ", true)

#ifdef STM_ONESHOT_ALG_CohortsQ
DECLARE_AS_ONESHOT(CohortsQ)
#endif
