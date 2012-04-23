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

#include "profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

// define atomic operations
#define CAS __sync_val_compare_and_swap
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch

using stm::TxThread;
using stm::threads;
using stm::threadcount;
using stm::last_complete;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::global_filter;
using stm::started;
using stm::cpending;
using stm::committed;
using stm::last_order;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  NOINLINE bool validate(TxThread* tx);

  struct CohortsFilter {
      static TM_FASTCALL bool begin(TxThread*);
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void commit_ro(TxThread* tx);
      static TM_FASTCALL void commit_rw(TxThread* tx);

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
  };

  /**
   *  CohortsFilter begin:
   *  CohortsFilter has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  bool
  CohortsFilter::begin(TxThread* tx)
  {
    S1:
      // wait until everyone is committed
      while (cpending.val != committed.val)
          spin64();

      // before tx begins, increase total number of tx
      ADD(&started.val, 1);

      // [NB] we must double check no one is ready to commit yet!
      if (cpending.val > committed.val) {
          SUB(&started.val, 1);
          goto S1;
      }

      tx->allocator.onTxBegin();
      return true;
  }

  /**
   *  CohortsFilter commit (read-only):
   */
  void
  CohortsFilter::commit_ro(TxThread* tx)
  {
      // decrease total number of tx started
      SUB(&started.val, 1);

      // clean up
      tx->rf->clear();
      OnReadOnlyCommit(tx);
  }

  /**
   *  CohortsFilter commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsFilter::commit_rw(TxThread* tx)
  {
      // increment num of tx ready to commit, and use it as the order
      tx->order = ADD(&cpending.val, 1);

      // Wait until all tx are ready to commit
      while (cpending.val < started.val)
          spin64();

      // Wait for my turn
      // [mfs] this is the start of the critical section
      while (last_complete.val != (uintptr_t)(tx->order - 1))
          spin64();

      // If I'm not the first one in a cohort to commit, validate read
      if (tx->order != last_order)
          if (!validate(tx)) {
              committed.val++;
              CFENCE;
              last_complete.val = tx->order;
              tx->tmabort(tx);
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
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsFilter read (read-only transaction)
   */
  void*
  CohortsFilter::read_ro(STM_READ_SIG(tx,addr,))
  {
      tx->rf->add(addr);
      return *addr;
  }

  /**
   *  CohortsFilter read (writing transaction)
   */
  void*
  CohortsFilter::read_rw(STM_READ_SIG(tx,addr,mask))
  {
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
  CohortsFilter::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  CohortsFilter write (writing context)
   */
  void
  CohortsFilter::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
  }

  /**
   *  CohortsFilter unwinder:
   */
  stm::scope_t*
  CohortsFilter::rollback(STM_ROLLBACK_SIG(tx, except, len))
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

      return PostRollback(tx);
  }

  /**
   *  CohortsFilter in-flight irrevocability:
   */
  bool
  CohortsFilter::irrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsFilter Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsFilter validation for commit: check that all reads are valid
   */
  bool
  validate(TxThread* tx)
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
  CohortsFilter::onSwitchTo()
  {
      last_complete.val = 0;
      global_filter->clear();
  }
}

namespace stm {
  /**
   *  CohortsFilter initialization
   */
  template<>
  void initTM<CohortsFilter>()
  {
      // set the name
      stms[CohortsFilter].name      = "CohortsFilter";
      // set the pointers
      stms[CohortsFilter].begin     = ::CohortsFilter::begin;
      stms[CohortsFilter].commit    = ::CohortsFilter::commit_ro;
      stms[CohortsFilter].read      = ::CohortsFilter::read_ro;
      stms[CohortsFilter].write     = ::CohortsFilter::write_ro;
      stms[CohortsFilter].rollback  = ::CohortsFilter::rollback;
      stms[CohortsFilter].irrevoc   = ::CohortsFilter::irrevoc;
      stms[CohortsFilter].switcher  = ::CohortsFilter::onSwitchTo;
      stms[CohortsFilter].privatization_safe = true;
  }
}

