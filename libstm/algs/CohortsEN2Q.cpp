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
 *  CohortsEN2Q Implementation
 *
 *  CohortsEN2Q is CohortsNorec with inplace write if I'm the last one in the
 *  cohort. (Relexed CONDITION TO GO TURBO.)
 *  Use Queue to handle ordered commit.
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  TM_FASTCALL bool CohortsEN2QValidate(TxThread* tx);
  void Begin(TX_LONE_PARAMETER);
  TM_FASTCALL void* CohortsEN2QReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsEN2QReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsEN2QReadTurbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsEN2QWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsEN2QWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsEN2QWriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsEN2QCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsEN2QCommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsEN2QCommitTurbo(TX_LONE_PARAMETER);

  /**
   *  CohortsEN2Q begin:
   *  CohortsEN2Q has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsEN2QBegin(TX_LONE_PARAMETER)
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

      // reset tx->status;
      tx->status = COHORTS_NOTURBO;

      // reset local turn val
      tx->turn.val = COHORTS_NOTDONE;
  }

  /**
   *  CohortsEN2Q commit (read-only):
   */
  void
  CohortsEN2QCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faaptr(&started.val, -1);

      // clean up
      tx->vlist.reset();
      OnROCommit(tx);
  }

  /**
   *  CohortsEN2Q commit (in place write commit): no validation, no write back
   */
  void
  CohortsEN2QCommitTurbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faaptr(&started.val, -1);

      // clean up
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsEN2QReadRO, CohortsEN2QWriteRO, CohortsEN2QCommitRO);
  }

  /**
   *  CohortsEN2Q commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsEN2QCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // add myself to the queue
      do {
          tx->turn.next = q;
      }while (!bcasptr(&q, tx->turn.next, &(tx->turn)));

      // decrease total number of tx started
      uint32_t temp = faaptr(&started.val, -1) - 1;

      // If I'm the next to the last, notify the last txn to go turbo
      if (temp == 1)
          for (uint32_t i = 0; i < threadcount.val; i++)
              threads[i]->status = COHORTS_TURBO;

      // Wait for my turn
      if (tx->turn.next != NULL)
          while (tx->turn.next->val != COHORTS_DONE);

      // Wait until all tx are ready to commit
      while (started.val != 0);

      // Everyone must validate read
      if (!CohortsEN2QValidate(tx)) {
          tx->turn.val = COHORTS_DONE;
          if (q == &(tx->turn)) q = NULL;
          tmabort();
      }

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
      ResetToRO(tx, CohortsEN2QReadRO, CohortsEN2QWriteRO, CohortsEN2QCommitRO);
  }

  /**
   *  CohortsEN2Q ReadTurbo
   */
  void*
  CohortsEN2QReadTurbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsEN2Q read (read-only transaction)
   */
  void*
  CohortsEN2QReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      void *tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      // test if I can go turbo
      if (tx->status == COHORTS_TURBO)
          GoTurbo(tx, CohortsEN2QReadTurbo, CohortsEN2QWriteTurbo, CohortsEN2QCommitTurbo);
      return tmp;
  }

  /**
   *  CohortsEN2Q read (writing transaction)
   */
  void*
  CohortsEN2QReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      void* tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      REDO_RAW_CLEANUP(tmp, found, log, mask);
      // test if I can go turbo
      if (tx->status == COHORTS_TURBO) {
          tx->writes.writeback();
          GoTurbo(tx, CohortsEN2QReadTurbo, CohortsEN2QWriteTurbo, CohortsEN2QCommitTurbo);
      }
      return tmp;
  }

  /**
   *  CohortsEN2Q write (read-only context): for first write
   */
  void
  CohortsEN2QWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      if (tx->status == COHORTS_TURBO) {
          // in place write
          *addr = val;
          // go turbo mode
          GoTurbo(tx, CohortsEN2QReadTurbo, CohortsEN2QWriteTurbo, CohortsEN2QCommitTurbo);
          return;
      }
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CohortsEN2QReadRW, CohortsEN2QWriteRW, CohortsEN2QCommitRW);
  }

  /**
   *  CohortsEN2Q write (in place write)
   */
  void
  CohortsEN2QWriteTurbo(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      *addr = val; // in place write
  }

  /**
   *  CohortsEN2Q write (writing context)
   */
  void
  CohortsEN2QWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      if (tx->status == COHORTS_TURBO) {
          // write previous write set back
          foreach (WriteSet, i, tx->writes)
              *i->addr = i->val;
          CFENCE;
          // in place write
          *addr = val;
          // go turbo mode
          GoTurbo(tx, CohortsEN2QReadTurbo, CohortsEN2QWriteTurbo, CohortsEN2QCommitTurbo);
          return;
      }
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsEN2Q unwinder:
   */
  void
  CohortsEN2QRollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsEN2Q in-flight irrevocability:
   */
  bool
  CohortsEN2QIrrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsEN2Q Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsEN2Q validation for commit: check that all reads are valid
   */
  bool CohortsEN2QValidate(TxThread* tx)
  {
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid) return false;
      }
      return true;
  }

  /**
   *  Switch to CohortsEN2Q:
   *
   */
  void
  CohortsEN2QOnSwitchTo()
  {
  }
}


DECLARE_SIMPLE_METHODS_FROM_TURBO(CohortsEN2Q)
REGISTER_FGADAPT_ALG(CohortsEN2Q, "CohortsEN2Q", true)

#ifdef STM_ONESHOT_ALG_CohortsEN2Q
DECLARE_AS_ONESHOT(CohortsEN2Q)
#endif
