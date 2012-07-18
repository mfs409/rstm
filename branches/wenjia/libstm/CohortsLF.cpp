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
#include "profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

// define atomic operations
#define CAS __sync_val_compare_and_swap
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch

#define COHORTS_COMMITTED 0
#define COHORTS_STARTED   1
#define COHORTS_CPENDING  2

using stm::TxThread;
using stm::threads;
using stm::threadcount;
using stm::last_complete;
using stm::timestamp;
using stm::timestamp_max;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::gatekeeper;
using stm::last_order;
using stm::global_filter;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  struct CohortsLF {
      static TM_FASTCALL bool begin();
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro();
      static TM_FASTCALL void commit_rw();

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE void validate(TxThread* tx);
  };

  /**
   *  CohortsLF begin:
   *  CohortsLF has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  bool
  CohortsLF::begin()
  {
      TxThread* tx = stm::Self;
    S1:
      // wait if I'm blocked
      while(gatekeeper == 1)
          spin64();

      // set started
      tx->status = COHORTS_STARTED;
      WBR;

      // double check no one is ready to commit
      if (gatekeeper == 1){
          tx->status = COHORTS_COMMITTED;
          goto S1;
      }

      //begin
      tx->allocator.onTxBegin();

      return true;
  }

  /**
   *  CohortsLF commit (read-only):
   */
  void
  CohortsLF::commit_ro()
  {
      TxThread* tx = stm::Self;
      // mark self status
      tx->status = COHORTS_COMMITTED;

      // clean up
      tx->rf->clear();
      OnReadOnlyCommit(tx);
  }

  /**
   *  CohortsLF commit (writing context):
   *
   */
  void
  CohortsLF::commit_rw()
  {
      TxThread* tx = stm::Self;
      // Mark a global flag, no one is allowed to begin now
      gatekeeper = 1;

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
      if (tx->order != last_order)
          validate(tx);

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
          last_order = tx->order + 1;
          global_filter->clear();
          gatekeeper = 0;
      }

      // commit all frees, reset all lists
      tx->rf->clear();
      tx->wf->clear();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsLF read (read-only transaction)
   */
  void*
  CohortsLF::read_ro(STM_READ_SIG(addr,))
  {
      TxThread* tx = stm::Self;
      tx->rf->add(addr);
      return *addr;
  }

  /**
   *  CohortsLF read (writing transaction)
   */
  void*
  CohortsLF::read_rw(STM_READ_SIG(addr,mask))
  {
      TxThread* tx = stm::Self;
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
  CohortsLF::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  CohortsLF write (writing context)
   */
  void
  CohortsLF::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
  }

  /**
   *  CohortsLF unwinder:
   */
  stm::scope_t*
  CohortsLF::rollback(STM_ROLLBACK_SIG(tx, except, len))
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

      return PostRollback(tx);
  }

  /**
   *  CohortsLF in-flight irrevocability:
   */
  bool
  CohortsLF::irrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsLF Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsLF validation for commit: check that all reads are valid
   */
  void
  CohortsLF::validate(TxThread* tx)
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
              last_order = tx->order + 1;
              global_filter->clear();
              gatekeeper = 0;
          }
          tx->tmabort(tx);
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
  CohortsLF::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = timestamp.val;
      // when switching algs, mark all tx committed status
      for (uint32_t i = 0; i < threadcount.val; ++i) {
          threads[i]->status = COHORTS_COMMITTED;
      }
      global_filter->clear();
  }
}

namespace stm {
  /**
   *  CohortsLF initialization
   */
  template<>
  void initTM<CohortsLF>()
  {
      // set the name
      stms[CohortsLF].name      = "CohortsLF";
      // set the pointers
      stms[CohortsLF].begin     = ::CohortsLF::begin;
      stms[CohortsLF].commit    = ::CohortsLF::commit_ro;
      stms[CohortsLF].read      = ::CohortsLF::read_ro;
      stms[CohortsLF].write     = ::CohortsLF::write_ro;
      stms[CohortsLF].rollback  = ::CohortsLF::rollback;
      stms[CohortsLF].irrevoc   = ::CohortsLF::irrevoc;
      stms[CohortsLF].switcher  = ::CohortsLF::onSwitchTo;
      stms[CohortsLF].privatization_safe = true;
  }
}

