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
 *  CohortsEF Implementation
 *  CohortsEager with Filter
 *
 *  See notes in Eager and Filter implementations
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  NOINLINE bool CohortsEFValidate(TxThread* tx);

  TM_FASTCALL void* CohortsEFReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsEFReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsEFReadTurbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsEFWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsEFWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsEFWriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsEFCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsEFCommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsEFCommitTurbo(TX_LONE_PARAMETER);

  /**
   *  CohortsEF begin:
   *  CohortsEF has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsEFBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
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

      tx->allocator.onTxBegin();
  }

  /**
   *  CohortsEF commit (read-only):
   */
  void
  CohortsEFCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faaptr(&started.val, -1);

      // clean up
      tx->rf->clear();
      OnROCommit(tx);
  }

  /**
   *  CohortsEF commit (in place write commit): no validation, no write back
   *  no other thread touches cpending.
   */
  void
  CohortsEFCommitTurbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // increase # of tx waiting to commit, and use it as the order
      tx->order = 1 + faiptr(&cpending.val);

      // clean up
      tx->rf->clear();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsEFReadRO, CohortsEFWriteRO, CohortsEFCommitRO);

      // wait for my turn, in this case, cpending is my order
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // I must be the last in the cohort, so clean global_filter
      global_filter->clear();

      // reset in place write flag
      inplace.val = 0;

      // increase # of committed
      committed.val ++;
      CFENCE;

      // mark self as done
      last_complete.val = tx->order;
  }

  /**
   *  CohortsEF commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsEFCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // increase # of tx waiting to commit, and use it as the order
      tx->order = 1 + faiptr(&cpending.val);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // Wait until all tx are ready to commit
      while (cpending.val < started.val);

      // If in place write occurred, all tx validate reads
      // Otherwise, only first one skips validation
      if (inplace.val == 1 || tx->order != last_order.val)
          if (!CohortsEFValidate(tx)) {
              committed.val ++;
              CFENCE;
              last_complete.val = tx->order;
              tmabort();
          }

      // do write back
      tx->writes.writeback();
      WBR;

      //union tx local write filter with the global filter
      global_filter->unionwith (*(tx->wf));
      WBR;

      // If the last one in the cohort, save the order and clear the filter
      if ((uint32_t)tx->order == started.val) {
          last_order.val = started.val + 1;
          global_filter->clear();
      }

      // increase total number of committed tx
      committed.val ++;
      CFENCE;

      // mark self as done
      last_complete.val = tx->order;

      // commit all frees, reset all lists
      tx->rf->clear();
      tx->wf->clear();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsEFReadRO, CohortsEFWriteRO, CohortsEFCommitRO);
  }

  /**
   *  CohortsEF ReadTurbo
   */
  void*
  CohortsEFReadTurbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsEF read (read-only transaction)
   */
  void*
  CohortsEFReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      tx->rf->add(addr);
      return *addr;
  }

  /**
   *  CohortsEF read (writing transaction)
   */
  void*
  CohortsEFReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      tx->rf->add(addr);

      void* val = *addr;
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  CohortsEF write (read-only context): for first write
   */
  void
  CohortsEFWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // If everyone else is ready to commit, do in place write
      if (cpending.val + 1 == started.val) {
          // set up flag indicating in place write starts
          atomicswapptr(&inplace.val, 1);
          // double check is necessary
          if (cpending.val + 1 == started.val) {
              // in place write
              *addr = val;
              // add entry to the global filter
              global_filter->add(addr);
              // go turbo mode
              OnFirstWrite(tx, CohortsEFReadTurbo, CohortsEFWriteTurbo, CohortsEFCommitTurbo);
              return;
          }
          // reset flag
          inplace.val = 0;
      }
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
      OnFirstWrite(tx, CohortsEFReadRW, CohortsEFWriteRW, CohortsEFCommitRW);
  }

  /**
   *  CohortsEF write (in place write)
   */
  void
  CohortsEFWriteTurbo(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      *addr = val; // in place write
      // add entry to the global filter
      global_filter->add(addr);
  }

  /**
   *  CohortsEF write (writing context)
   */
  void
  CohortsEFWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
  }

  /**
   *  CohortsEF unwinder:
   */
  void
  CohortsEFRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists
      tx->rf->clear();
      if (tx->writes.size()) {
          tx->wf->clear();
          tx->writes.reset();
      }
      PostRollback(tx);
  }

  /**
   *  CohortsEF in-flight irrevocability:
   */
  bool
  CohortsEFIrrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsEF Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsEF validation for commit: check that all reads are valid
   */
  bool CohortsEFValidate(TxThread* tx)
  {
      // If there is a same element in both global_filter and read_filter
      if (global_filter->intersect(tx->rf)) {
          // I'm the last one in the cohort, save the order and clear the filter
          if ((uint32_t)tx->order == started.val) {
              last_order.val = started.val + 1;
              global_filter->clear();
          }
          return false;
      }
      return true;
  }

  /**
   *  Switch to CohortsEF:
   *
   */
  void
  CohortsEFOnSwitchTo()
  {
      last_complete.val = 0;
      global_filter->clear();
  }

  /**
   *  CohortsEF initialization
   */
  template<>
  void initTM<CohortsEF>()
  {
      // set the name
      stms[CohortsEF].name      = "CohortsEF";
      // set the pointers
      stms[CohortsEF].begin     = CohortsEFBegin;
      stms[CohortsEF].commit    = CohortsEFCommitRO;
      stms[CohortsEF].read      = CohortsEFReadRO;
      stms[CohortsEF].write     = CohortsEFWriteRO;
      stms[CohortsEF].rollback  = CohortsEFRollback;
      stms[CohortsEF].irrevoc   = CohortsEFIrrevoc;
      stms[CohortsEF].switcher  = CohortsEFOnSwitchTo;
      stms[CohortsEF].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_CohortsEF
DECLARE_AS_ONESHOT_TURBO(CohortsEF)
#endif
