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
 *  CohortsLF Implementation
 *  CohortsLazy with filter for validations.
 *
 *  [mfs] see notes for lazy and filter implementations
 */
#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  TM_FASTCALL void* CohortsLFReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsLFReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsLFWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsLFWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsLFCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsLFCommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsLFValidate(TxThread* tx);

  /**
   *  CohortsLF begin:
   *  CohortsLF has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsLFBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
    S1:
      // wait if I'm blocked
      while(gatekeeper.val == 1)
          spin64();

      // set started
      tx->status = COHORTS_STARTED;
      WBR;

      // double check no one is ready to commit
      if (gatekeeper.val == 1){
          tx->status = COHORTS_COMMITTED;
          goto S1;
      }

      //begin
      tx->allocator.onTxBegin();
  }

  /**
   *  CohortsLF commit (read-only):
   */
  void
  CohortsLFCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // mark self status
      tx->status = COHORTS_COMMITTED;

      // clean up
      tx->rf->clear();
      OnROCommit(tx);
  }

  /**
   *  CohortsLF commit (writing context):
   *
   */
  void
  CohortsLFCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Mark a global flag, no one is allowed to begin now
      gatekeeper.val = 1;

      // Mark self pending to commit
      tx->status = COHORTS_CPENDING;

      // Get an order
      tx->order = 1 + faiptr(&timestamp.val);

      // For later use, indicates if I'm the last tx in this cohort
      bool lastone = true;

      // Wait until all tx are ready to commit
      for (uint32_t i = 0; i < threadcount.val; ++i)
          while (threads[i]->status == COHORTS_STARTED);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // If I'm the first one in this cohort, no validation, else validate
      if (tx->order != last_order.val)
          CohortsLFValidate(tx);

      // do write back
      tx->writes.writeback();
      WBR;

      // union tx local write filter with the global filter
      global_filter->unionwith(*(tx->wf));
      // WBR;
      // Mark self as done
      last_complete.val = tx->order;

      // Mark self status
      tx->status = COHORTS_COMMITTED;
      WBR;

      // Am I the last one?
      for (uint32_t i = 0; lastone != false && i < threadcount.val; ++i)
          lastone &= (threads[i]->status != COHORTS_CPENDING);

      // If I'm the last one, release gatekeeper lock and clear global filter
      if (lastone) {
          last_order.val= tx->order + 1;
          global_filter->clear();
          gatekeeper.val = 0;
      }

      // commit all frees, reset all lists
      tx->rf->clear();
      tx->wf->clear();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsLFReadRO, CohortsLFWriteRO, CohortsLFCommitRO);
  }

  /**
   *  CohortsLF read (read-only transaction)
   */
  void*
  CohortsLFReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      tx->rf->add(addr);
      return *addr;
  }

  /**
   *  CohortsLF read (writing transaction)
   */
  void*
  CohortsLFReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
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
   *  CohortsLF write (read-only context): for first write
   */
  void
  CohortsLFWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
      OnFirstWrite(tx, CohortsLFReadRW, CohortsLFWriteRW, CohortsLFCommitRW);
  }

  /**
   *  CohortsLF write (writing context)
   */
  void
  CohortsLFWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
  }

  /**
   *  CohortsLF unwinder:
   */
  void
  CohortsLFRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists
      tx->rf->clear();
      if (tx->writes.size()) {
          tx->writes.reset();
          tx->wf->clear();
      }

      PostRollback(tx);
  }

  /**
   *  CohortsLF in-flight irrevocability:
   */
  bool
  CohortsLFIrrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsLF Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsLF validation for commit: check that all reads are valid
   */
  void
  CohortsLFValidate(TxThread* tx)
  {
      // If there is a same element in both global_filter and read_filter
      if (global_filter->intersect(tx->rf)) {
          // Mark self as done
          last_complete.val = tx->order;
          // Mark self status
          tx->status = COHORTS_COMMITTED;
          WBR;

          // Am I the last one?
          bool l = true;
          for (uint32_t i = 0; l != false && i < threadcount.val; ++i)
              l &= (threads[i]->status != COHORTS_CPENDING);

          // If I'm the last one, release gatekeeper lock
          if (l) {
              last_order.val = tx->order + 1;
              global_filter->clear();
              gatekeeper.val = 0;
          }
          tmabort();
      }
  }

  /**
   *  Switch to CohortsLF:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   */
  void
  CohortsLFOnSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = timestamp.val;
      // when switching algs, mark all tx committed status
      for (uint32_t i = 0; i < threadcount.val; ++i) {
          threads[i]->status = COHORTS_COMMITTED;
      }
      global_filter->clear();
  }

  /**
   *  CohortsLF initialization
   */
  template<>
  void initTM<CohortsLF>()
  {
      // set the name
      stms[CohortsLF].name      = "CohortsLF";
      // set the pointers
      stms[CohortsLF].begin     = CohortsLFBegin;
      stms[CohortsLF].commit    = CohortsLFCommitRO;
      stms[CohortsLF].read      = CohortsLFReadRO;
      stms[CohortsLF].write     = CohortsLFWriteRO;
      stms[CohortsLF].rollback  = CohortsLFRollback;
      stms[CohortsLF].irrevoc   = CohortsLFIrrevoc;
      stms[CohortsLF].switcher  = CohortsLFOnSwitchTo;
      stms[CohortsLF].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_CohortsLF
DECLARE_AS_ONESHOT_NORMAL(CohortsLF)
#endif
