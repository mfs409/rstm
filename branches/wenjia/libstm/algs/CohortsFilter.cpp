/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.ISM for licensing information
 */

/**
 *  CohortsFilter Implementation
 *
 *  Cohorts using BitFilter for validations
 *
 * [mfs] We should have another version of this with TINY filters (eg 64 bits).
 *
 * [mfs] I am worried about the WBRs in this code.  It would seem that CFENCE
 *       would suffice.  The problem could relate to the use of SSE.  It would
 *       be good to verify that the WBRs can't be replaced with CFENCEs when
 *       SSE is turned off.  It would also be good to implement with 64-bit
 *       filters, which wouldn't use SSE, to see if that eliminated the need
 *       for WBR to get proper behavior.  It's possible that the WBR is just
 *       enforcing WAW behavior between SSE registers and non-SSE registers.
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  NOINLINE bool CohortsFilterValidate(TxThread* tx);
  TM_FASTCALL void* CohortsFilterReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsFilterReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsFilterWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsFilterWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsFilterCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsFilterCommitRW(TX_LONE_PARAMETER);

  /**
   *  CohortsFilter begin:
   *  CohortsFilter has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsFilterBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
    S1:
      // wait until everyone is committed
      while (cpending.val != committed.val)
          spin64();

      // before tx begins, increase total number of tx
      faiptr(&started.val);

      // [NB] we must double check no one is ready to commit yet!
      if (cpending.val > committed.val) {
          faaptr(&started.val, -1);
          goto S1;
      }

      tx->allocator.onTxBegin();
  }

  /**
   *  CohortsFilter commit (read-only):
   */
  void
  CohortsFilterCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faaptr(&started.val, -1);

      // clean up
      tx->rf->clear();
      OnROCommit(tx);
  }

  /**
   *  CohortsFilter commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsFilterCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // increment num of tx ready to commit, and use it as the order
      tx->order = 1 + faiptr(&cpending.val);

      // Wait until all tx are ready to commit
      while (cpending.val < started.val)
          spin64();

      // Wait for my turn
      // [mfs] this is the start of the critical section
      while (last_complete.val != (uintptr_t)(tx->order - 1))
          spin64();

      // If I'm not the first one in a cohort to commit, validate read
      if (tx->order != last_order)
          if (!CohortsFilterValidate(tx)) {
              committed.val++;
              CFENCE;
              last_complete.val = tx->order;
              tmabort();
          }

      // do write back
      tx->writes.writeback();
      // [NB] Intruder bench will abort if without this WBR fence
      // CFENCE doesn't work for 'intruder -t8'
      WBR;

      // union tx local write filter with the global filter
      global_filter->unionwith(*(tx->wf));
      CFENCE;

      // If I'm the last one in the cohort, save the order and clear the filter
      if ((uint32_t)tx->order == started.val) {
          last_order = tx->order + 1;
          global_filter->clear();
      }

      // Increase total tx committed
      committed.val++;
      CFENCE;

      // mark self as done
      // [mfs] this is the end of the critical section
      last_complete.val = tx->order;

      // commit all frees, reset all lists
      tx->rf->clear();
      tx->wf->clear();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsFilterReadRO, CohortsFilterWriteRO, CohortsFilterCommitRO);
  }

  /**
   *  CohortsFilter read (read-only transaction)
   */
  void*
  CohortsFilterReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      tx->rf->add(addr);
      return *addr;
  }

  /**
   *  CohortsFilter read (writing transaction)
   */
  void*
  CohortsFilterReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // log the address
      tx->rf->add(addr);

      void* val = *addr;
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  CohortsFilter write (read-only context): for first write
   */
  void
  CohortsFilterWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
      OnFirstWrite(tx, CohortsFilterReadRW, CohortsFilterWriteRW, CohortsFilterCommitRW);
  }

  /**
   *  CohortsFilter write (writing context)
   */
  void
  CohortsFilterWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
  }

  /**
   *  CohortsFilter unwinder:
   */
  void
  CohortsFilterRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists and filters
      tx->rf->clear();
      if (tx->writes.size()) {
          tx->writes.reset();
          tx->wf->clear();
      }

      PostRollback(tx);
  }

  /**
   *  CohortsFilter in-flight irrevocability:
   */
  bool
  CohortsFilterIrrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsFilter Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsFilter validation for commit: check that all reads are valid
   */
  bool
  CohortsFilterValidate(TxThread* tx)
  {
      // If there is a same element in both global_filter and read_filter
      if (global_filter->intersect(tx->rf)) {
          // I'm the last one in the cohort, save the order and clear the filter
          if ((uint32_t)tx->order == started.val) {
              last_order = started.val + 1;
              global_filter->clear();
          }
          return false;
      }
      return true;
  }

  /**
   *  Switch to CohortsFilter:
   *
   */
  void
  CohortsFilterOnSwitchTo()
  {
      last_complete.val = 0;
      global_filter->clear();
  }

  /**
   *  CohortsFilter initialization
   */
  template<>
  void initTM<CohortsFilter>()
  {
      // set the name
      stms[CohortsFilter].name      = "CohortsFilter";
      // set the pointers
      stms[CohortsFilter].begin     = CohortsFilterBegin;
      stms[CohortsFilter].commit    = CohortsFilterCommitRO;
      stms[CohortsFilter].read      = CohortsFilterReadRO;
      stms[CohortsFilter].write     = CohortsFilterWriteRO;
      stms[CohortsFilter].rollback  = CohortsFilterRollback;
      stms[CohortsFilter].irrevoc   = CohortsFilterIrrevoc;
      stms[CohortsFilter].switcher  = CohortsFilterOnSwitchTo;
      stms[CohortsFilter].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_CohortsFilter
DECLARE_AS_ONESHOT_NORMAL(CohortsFilter)
#endif
