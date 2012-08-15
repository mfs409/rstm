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
 *  CohortsEN Implementation
 *
 *  CohortsEN is CohortsNorec with inplace write if I'm the last one in the
 *  cohort.
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  TM_FASTCALL bool CohortsENValidate(TxThread* tx);

  TM_FASTCALL void* CohortsENReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsENReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsENReadTurbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsENWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsENWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsENWriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsENCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsENCommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsENCommitTurbo(TX_LONE_PARAMETER);

  /**
   *  CohortsEN begin:
   *  CohortsEN has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsENBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();

    S1:
      // wait until everyone is committed
      while (cpending.val != committed.val);

      // before tx begins, increase total number of tx
      faiptr(&started.val);

      // [NB] we must double check no one is ready to commit yet
      // and no one entered in place write phase(turbo mode)
      if (cpending.val > committed.val || inplace.val == 1){
          faaptr(&started.val, -1);
          goto S1;
      }
  }

  /**
   *  CohortsEN commit (read-only):
   */
  void
  CohortsENCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faaptr(&started.val, -1);

      // clean up
      tx->vlist.reset();
      OnROCommit(tx);
  }

  /**
   *  CohortsEN commit (in place write commit): no validation, no write back
   *  no other thread touches cpending
   */
  void
  CohortsENCommitTurbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // increase # of tx waiting to commit, and use it as the order
      tx->order = 1 + faiptr(&cpending.val);

      // clean up
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsENReadRO, CohortsENWriteRO, CohortsENCommitRO);

      // wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // reset in place write flag
      inplace.val = 0;

      // increase # of committed
      committed.val ++;
      CFENCE;

      // mark self as done
      last_complete.val = tx->order;
  }

  /**
   *  CohortsEN commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsENCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // order of first tx in cohort
      int32_t first = last_complete.val + 1;
      CFENCE;

      // increase # of tx waiting to commit, and use it as the order
      tx->order = 1 + faiptr(&cpending.val);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // Wait until all tx are ready to commit
      while (cpending.val < started.val);

      // If in place write occurred, all tx validate reads
      // Otherwise, only first one skips validation
      if (inplace.val == 1 || tx->order != first)
          if (!CohortsENValidate(tx)) {
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
      ResetToRO(tx, CohortsENReadRO, CohortsENWriteRO, CohortsENCommitRO);
  }

  /**
   *  CohortsEN ReadTurbo
   */
  void*
  CohortsENReadTurbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsEN read (read-only transaction)
   */
  void*
  CohortsENReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      void *tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsEN read (writing transaction)
   */
  void*
  CohortsENReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
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
   *  CohortsEN write (read-only context): for first write
   */
  void
  CohortsENWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {TX_GET_TX_INTERNAL;

#ifndef TRY
      // If everyone else is ready to commit, do in place write
      if (cpending.val + 1 == started.val) {
          // set up flag indicating in place write starts
          atomicswapptr(&inplace.val, 1);
          // double check is necessary
          if (cpending.val + 1 == started.val) {
              // in place write
              *addr = val;
              // go turbo mode
              OnFirstWrite(tx, CohortsENReadTurbo, CohortsENWriteTurbo, CohortsENCommitTurbo);
              return;
          }
          // reset flag
          inplace.val = 0;
      }
#endif
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CohortsENReadRW, CohortsENWriteRW, CohortsENCommitRW);
  }

  /**
   *  CohortsEN write (in place write)
   */
  void
  CohortsENWriteTurbo(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      *addr = val; // in place write
  }

  /**
   *  CohortsEN write (writing context)
   */
  void
  CohortsENWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
#ifdef TRY
      // Try to go turbo when "writes.size(TX_LONE_PARAMETER) >= TIMES"
      if (tx->writes.size(TX_LONE_PARAMETER) >= TIMES && cpending.val + 1 == started.val) {
          // set up flag indicating in place write starts
          atomicswapptr(&inplace.val, 1);
          // double check is necessary
          if (cpending.val + 1 == started.val) {
              // write back
              tx->writes.writeback(TX_LONE_PARAMETER);
              // in place write
              *addr = val;
              // go turbo mode
              OnFirstWrite(tx, ReadTurbo, WriteTurbo, CommitTurbo);
              return;
          }
          // reset flag
          inplace.val = 0;
      }
#endif
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsEN unwinder:
   */
  void
  CohortsENRollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsEN in-flight irrevocability:
   */
  bool
  CohortsENIrrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsEN Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsEN validation for commit: check that all reads are valid
   */
  bool CohortsENValidate(TxThread* tx)
  {
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid) return false;
      }
      return true;
  }

  /**
   *  Switch to CohortsEN:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   */
  void
  CohortsENOnSwitchTo()
  {
      last_complete.val = 0;
      inplace.val = 0;
  }

  /**
   *  CohortsEN initialization
   */
  template<>
  void initTM<CohortsEN>()
  {
      // set the name
      stms[CohortsEN].name      = "CohortsEN";
      // set the pointers
      stms[CohortsEN].begin     = CohortsENBegin;
      stms[CohortsEN].commit    = CohortsENCommitRO;
      stms[CohortsEN].read      = CohortsENReadRO;
      stms[CohortsEN].write     = CohortsENWriteRO;
      stms[CohortsEN].rollback  = CohortsENRollback;
      stms[CohortsEN].irrevoc   = CohortsENIrrevoc;
      stms[CohortsEN].switcher  = CohortsENOnSwitchTo;
      stms[CohortsEN].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_CohortsEN
DECLARE_AS_ONESHOT_TURBO(CohortsEN)
#endif
