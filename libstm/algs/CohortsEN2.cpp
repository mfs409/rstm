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
 *  CohortsEN2 Implementation
 *
 *  CohortsEN2 is CohortsNorec with inplace write if I'm the last one in the
 *  cohort. (Relexed CONDITION TO GO TURBO.)
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  TM_FASTCALL bool CohortsEN2Validate(TxThread* tx);
  TM_FASTCALL void* CohortsEN2ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsEN2ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsEN2ReadTurbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsEN2WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsEN2WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsEN2WriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsEN2CommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsEN2CommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsEN2CommitTurbo(TX_LONE_PARAMETER);

  /**
   *  CohortsEN2 begin:
   *  CohortsEN2 has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsEN2Begin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
    S1:
      // wait until everyone is committed
      while (cpending.val != committed.val);

      // before tx begins, increase total number of tx
      faiptr(&started.val);

      // [NB] we must double check no one is ready to commit yet
      if (cpending.val > committed.val) {
          faaptr(&started.val, -1);
          goto S1;
      }

      // reset tx->status;
      tx->status = COHORTS_NOTURBO;
  }

  /**
   *  CohortsEN2 commit (read-only):
   */
  void
  CohortsEN2CommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faaptr(&started.val, -1);

      // clean up
      tx->vlist.reset();
      OnROCommit(tx);
  }

  /**
   *  CohortsEN2 commit (in place write commit): no validation, no write back
   *  no other thread touches cpending
   */
  void
  CohortsEN2CommitTurbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // increase # of tx waiting to commit, and use it as the order
      tx->order = ++cpending.val;

      // clean up
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsEN2ReadRO, CohortsEN2WriteRO, CohortsEN2CommitRO);

      // wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // increase # of committed
      committed.val ++;
      CFENCE;

      // mark self as done
      last_complete.val = tx->order;
  }

  /**
   *  CohortsEN2 commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsEN2CommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // increase # of tx waiting to commit, and use it as the order
      tx->order = 1+ faiptr(&cpending.val);

      // If I'm the next to the last, notify the last txn to go turbo
      if (tx->order == (intptr_t)started.val - 1)
          for (uint32_t i = 0; i < threadcount.val; i++)
              threads[i]->status = COHORTS_TURBO;

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // Wait until all tx are ready to commit
      while (cpending.val < started.val);

      // Everyone must validate read
      if (!CohortsEN2Validate(tx)) {
          committed.val++;
          CFENCE;
          last_complete.val = tx->order;
          tmabort();
      }

      // do write back
      tx->writes.writeback();

      // increase total number of committed tx
      committed.val++;
      CFENCE;

      // mark self as done
      last_complete.val = tx->order;

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsEN2ReadRO, CohortsEN2WriteRO, CohortsEN2CommitRO);
  }

  /**
   *  CohortsEN2 ReadTurbo
   */
  void*
  CohortsEN2ReadTurbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsEN2 read (read-only transaction)
   */
  void*
  CohortsEN2ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      void *tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsEN2 read (writing transaction)
   */
  void*
  CohortsEN2ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
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
   *  CohortsEN2 write (read-only context): for first write
   */
  void
  CohortsEN2WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      if (tx->status == COHORTS_TURBO) {
          // in place write
          *addr = val;
          // go turbo mode
          OnFirstWrite(tx, CohortsEN2ReadTurbo, CohortsEN2WriteTurbo, CohortsEN2CommitTurbo);
          return;
      }

      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CohortsEN2ReadRW, CohortsEN2WriteRW, CohortsEN2CommitRW);
  }

  /**
   *  CohortsEN2 write (in place write)
   */
  void
  CohortsEN2WriteTurbo(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      *addr = val; // in place write
  }

  /**
   *  CohortsEN2 write (writing context)
   */
  void
  CohortsEN2WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
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
          OnFirstWrite(tx, CohortsEN2ReadTurbo, CohortsEN2WriteTurbo, CohortsEN2CommitTurbo);
          return;
      }
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsEN2 unwinder:
   */
  void
  CohortsEN2Rollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsEN2 in-flight irrevocability:
   */
  bool
  CohortsEN2Irrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsEN2 Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsEN2 validation for commit: check that all reads are valid
   */
  bool
  CohortsEN2Validate(TxThread* tx)
  {
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid) return false;
      }
      return true;
  }

  /**
   *  Switch to CohortsEN2:
   *
   */
  void
  CohortsEN2OnSwitchTo()
  {
      last_complete.val = 0;
  }

  /**
   *  CohortsEN2 initialization
   */
  template<>
  void initTM<CohortsEN2>()
  {
      // set the name
      stms[CohortsEN2].name      = "CohortsEN2";
      // set the pointers
      stms[CohortsEN2].begin     = CohortsEN2Begin;
      stms[CohortsEN2].commit    = CohortsEN2CommitRO;
      stms[CohortsEN2].read      = CohortsEN2ReadRO;
      stms[CohortsEN2].write     = CohortsEN2WriteRO;
      stms[CohortsEN2].rollback  = CohortsEN2Rollback;
      stms[CohortsEN2].irrevoc   = CohortsEN2Irrevoc;
      stms[CohortsEN2].switcher  = CohortsEN2OnSwitchTo;
      stms[CohortsEN2].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_CohortsEN2
DECLARE_AS_ONESHOT_TURBO(CohortsEN2)
#endif
