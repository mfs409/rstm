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
 *  CohortsLNI Implementation
 *
 *  CohortsLazy with inplace write when tx is the last one in a cohort.
 */
#include "../profiling.hpp"
#include "../algs.hpp"
#include "../RedoRAWUtils.hpp"
#include "../Diagnostics.hpp"

using stm::TxThread;
using stm::threads;
using stm::threadcount;
using stm::last_complete;
using stm::timestamp;
using stm::timestamp_max;
using stm::WriteSet;
using stm::WriteSetEntry;

using stm::ValueList;
using stm::ValueListEntry;
using stm::gatekeeper;
using stm::last_order;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  volatile uintptr_t inplace = 0;

  struct CohortsLNI {
      static void begin(TX_LONE_PARAMETER);
      static TM_FASTCALL void* read_ro(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro(TX_LONE_PARAMETER);
      static TM_FASTCALL void commit_rw(TX_LONE_PARAMETER);

      static TM_FASTCALL void commit_turbo(TX_LONE_PARAMETER);
      static TM_FASTCALL void* read_turbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void write_turbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE void validate(TxThread* tx);
  };

  /**
   *  CohortsLNI begin:
   *  CohortsLNI has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsLNI::begin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      //begin
      tx->allocator.onTxBegin();

    S1:
      // wait if I'm blocked
      while (gatekeeper == 1);

      // set started
      //
      atomicswapptr(&tx->status, COHORTS_STARTED);
      //      tx->status = COHORTS_STARTED;
      //WBR;

      // double check no one is ready to commit
      if (gatekeeper == 1 || inplace == 1){
          tx->status = COHORTS_COMMITTED;
          goto S1;
      }

      // get time of last finished txn
      tx->ts_cache = last_complete.val;
  }

  /**
   *  CohortsLNI commit (read-only):
   */
  void
  CohortsLNI::commit_ro(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // mark self status
      tx->status = COHORTS_COMMITTED;

      // clean up
      tx->vlist.reset();
      OnROCommit(tx);
  }

  /**
   *  CohortsLNI commit_turbo (for write in place tx use):
   *
   */
  void
  CohortsLNI::commit_turbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Mark self pending to commit
      tx->status = COHORTS_CPENDING;

      // Get order
      tx->order = 1 + faiptr(&timestamp.val);

      // Turbo tx can clean up first
      tx->vlist.reset();
      OnRWCommit(tx);
      ResetToRO(tx, read_ro, write_ro, commit_ro);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // Mark self as done
      last_complete.val = tx->order;

      // I must be the last one, so release gatekeeper lock
      last_order = tx->order + 1;
      gatekeeper = 0;
      // Reset inplace write flag
      inplace = 0;

      // Mark self status
      tx->status = COHORTS_COMMITTED;
  }


  /**
   *  CohortsLNI commit (writing context):
   *
   */
  void
  CohortsLNI::commit_rw(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Mark a global flag, no one is allowed to begin now
      gatekeeper = 1;

      // Get an order
      tx->order = 1 + faiptr(&timestamp.val);

      // Mark self pending to commit
      tx->status = COHORTS_CPENDING;

      // For later use, indicates if I'm the last tx in this cohort
      bool lastone = true;

      // Wait until all tx are ready to commit
      for (uint32_t i = 0; i < threadcount.val; ++i)
          while (threads[i]->status == COHORTS_STARTED);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // If I'm the first one in this cohort and no inplace write happened,
      // I will do no validation, else validate
      if (inplace == 1 || tx->order != last_order)
          validate(tx);

      // Do write back
      tx->writes.writeback();

      CFENCE;
      // Mark self as done
      last_complete.val = tx->order;

      // Mark self status
      tx->status = COHORTS_COMMITTED;
      WBR;// this one cannot be omitted...

      // Am I the last one?
      for (uint32_t i = 0;lastone != false && i < threadcount.val; ++i)
          lastone &= (threads[i]->status != COHORTS_CPENDING);

      // If I'm the last one, release gatekeeper lock
      if (lastone) {
          last_order = tx->order + 1;
          gatekeeper = 0;
      }

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsLNI read (read-only transaction)
   */
  void*
  CohortsLNI::read_ro(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      void *tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsLNI read_turbo (for write in place tx use)
   */
  void*
  CohortsLNI::read_turbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsLNI read (writing transaction)
   */
  void*
  CohortsLNI::read_rw(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
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
   *  CohortsLNI write (read-only context): for first write
   */
  void
  CohortsLNI::write_ro(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // [mfs] this code is not in the best location.  Consider the following
      // alternative:
      //
      // - when a thread reaches the commit function, it seals the cohort
      // - then it counts the number of transactions in the cohort
      // - then it waits for all of them to finish
      // - while waiting, it eventually knows when there is exactly one left.
      // - at that point, it can set a flag to indicate that the last one is
      //   in-flight.
      // - all transactions can check that flag on every read/write
      //
      // There are a few challenges.  First, the current code waits on the
      // first thread, then the next, then the next...  Obviously that won't do
      // anymore.  Second, there can be a "flicker" when a thread sets a flag,
      // then reads the gatekeeper, then backs out.  Lastly, RO transactions
      // will require some sort of special attention.  But the tradeoff is more
      // potential to switch (not just first write), and without so much
      // redundant checking.

      int32_t count = 0;
      // scan to check others' status
      for (uint32_t i = 0; i < threadcount.val && count < 2; ++i)
          count += (threads[i]->status == COHORTS_STARTED);

      // If every one else is ready to commit, do in place write, go turbo mode
      if (count == 1) {
          // setup in place write flag
          atomicswapptr(&inplace, 1);

          // double check
          for (uint32_t i = 0; i < threadcount.val && count < 0; ++i)
              count -= (threads[i]->status == COHORTS_STARTED);
          if (count == 0) {
              // write inplace
              write_turbo(TX_FIRST_ARG addr, val);
              // go turbo
              stm::OnFirstWrite(tx, read_turbo, write_turbo, commit_turbo);
              return;
          }
          // reset flag
          inplace = 0;
      }
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      stm::OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }
  /**
   *  CohortsLNI write_turbo: for write in place tx
   */
  void
  CohortsLNI::write_turbo(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      *addr = val;
  }


  /**
   *  CohortsLNI write (writing context)
   */
  void
  CohortsLNI::write_rw(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsLNI unwinder:
   */
  void
  CohortsLNI::rollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsLNI in-flight irrevocability:
   */
  bool
  CohortsLNI::irrevoc(TxThread*)
  {
      stm::UNRECOVERABLE("CohortsLNI Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsLNI validation for commit: check that all reads are valid
   */
  void CohortsLNI::validate(TxThread* tx)
  {
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid) {
              // Mark self status
              tx->status = COHORTS_COMMITTED;

              // Mark self as done
              last_complete.val = tx->order;
              //WBR;

              // Am I the last one?
              bool l = true;
              for (uint32_t i = 0; l != false && i < threadcount.val; ++i)
                  l &= (threads[i]->status != COHORTS_CPENDING);

              // If I'm the last one, release gatekeeper lock
              if (l) {
                  last_order = tx->order + 1;
                  gatekeeper = 0;
              }
              stm::tmabort();
          }
      }
  }

  /**
   *  Switch to CohortsLNI:
   *
   */
  void CohortsLNI::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = timestamp.val;
      // when switching algs, mark all tx committed status
      for (uint32_t i = 0; i < threadcount.val; ++i) {
          threads[i]->status = COHORTS_COMMITTED;
      }
  }
}

namespace stm
{
  /**
   *  CohortsLNI initialization
   */
  template<>
  void initTM<CohortsLNI>()
  {
      // set the name
      stms[CohortsLNI].name      = "CohortsLNI";
      // set the pointers
      stms[CohortsLNI].begin     = ::CohortsLNI::begin;
      stms[CohortsLNI].commit    = ::CohortsLNI::commit_ro;
      stms[CohortsLNI].read      = ::CohortsLNI::read_ro;
      stms[CohortsLNI].write     = ::CohortsLNI::write_ro;
      stms[CohortsLNI].rollback  = ::CohortsLNI::rollback;
      stms[CohortsLNI].irrevoc   = ::CohortsLNI::irrevoc;
      stms[CohortsLNI].switcher  = ::CohortsLNI::onSwitchTo;
      stms[CohortsLNI].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_CohortsLNI
DECLARE_AS_ONESHOT_TURBO(CohortsLNI)
#endif
