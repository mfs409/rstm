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
 *  Cohorts3 Implementation
 *
 *  CohortsNOrec with a queue to handle order
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  NOINLINE bool Cohorts3Validate(TxThread* tx);
  TM_FASTCALL void* Cohorts3ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* Cohorts3ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void Cohorts3WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void Cohorts3WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void Cohorts3CommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void Cohorts3CommitRW(TX_LONE_PARAMETER);

  /**
   *  Cohorts3 begin:
   *  Cohorts3 has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void Cohorts3Begin(TX_LONE_PARAMETER)
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
   *  Cohorts3 commit (read-only):
   */
  void
  Cohorts3CommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faaptr(&started.val, -1);

      // clean up
      tx->vlist.reset();
      OnROCommit(tx);
  }

  /**
   *  Cohorts3 commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  Cohorts3CommitRW(TX_LONE_PARAMETER)
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
          if (!Cohorts3Validate(tx)) {
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
      ResetToRO(tx, Cohorts3ReadRO, Cohorts3WriteRO, Cohorts3CommitRO);
  }

  /**
   *  Cohorts3 read (read-only transaction)
   */
  void*
  Cohorts3ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      void * tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  Cohorts3 read (writing transaction)
   */
  void*
  Cohorts3ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
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
   *  Cohorts3 write (read-only context): for first write
   */
  void
  Cohorts3WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, Cohorts3ReadRW, Cohorts3WriteRW, Cohorts3CommitRW);
  }

  /**
   *  Cohorts3 write (writing context)
   */
  void
  Cohorts3WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  Cohorts3 unwinder:
   */
  void
  Cohorts3Rollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  Cohorts3 in-flight irrevocability:
   */
  bool
  Cohorts3Irrevoc(TxThread*)
  {
      UNRECOVERABLE("Cohorts3 Irrevocability not yet supported");
      return false;
  }

  /**
   *  Cohorts3 validation for commit
   */
  bool
  Cohorts3Validate(TxThread* tx)
  {
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid) return false;
      }
      return true;
  }


  /**
   *  Switch to Cohorts3:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   */
  void
  Cohorts3OnSwitchTo()
  {
  }

  /**
   *  Cohorts3 initialization
   */
  template<>
  void initTM<Cohorts3>()
  {
      // set the name
      stms[Cohorts3].name      = "Cohorts3";
      // set the pointers
      stms[Cohorts3].begin     = Cohorts3Begin;
      stms[Cohorts3].commit    = Cohorts3CommitRO;
      stms[Cohorts3].read      = Cohorts3ReadRO;
      stms[Cohorts3].write     = Cohorts3WriteRO;
      stms[Cohorts3].rollback  = Cohorts3Rollback;
      stms[Cohorts3].irrevoc   = Cohorts3Irrevoc;
      stms[Cohorts3].switcher  = Cohorts3OnSwitchTo;
      stms[Cohorts3].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_Cohorts3
DECLARE_AS_ONESHOT_NORMAL(Cohorts3)
#endif
