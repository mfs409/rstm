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
 *  CohortsLazy Implementation
 *
 *  Cohorts with only one CAS in commit_rw to get an order. Using
 *  txn local status instead of 3 global accumulators.
 *
 * "Lazy" isn't a good name for this... if I understand correctly, this is
 * Cohorts with a distributed mechanism for tracking the state of the cohort.
 */
#include "../profiling.hpp"
#include "../algs.hpp"
#include "../RedoRAWUtils.hpp"

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
using stm::WriteSet;
using stm::OrecList;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::orec_t;
using stm::get_orec;
using stm::gatekeeper;
using stm::last_order;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  struct CohortsLazy {
      static TM_FASTCALL bool begin();
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro();
      static TM_FASTCALL void commit_rw();

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE void validate(TxThread* tx);
  };

  /**
   *  CohortsLazy begin:
   *  CohortsLazy has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  bool
  CohortsLazy::begin()
  {
      TxThread* tx = stm::Self;
    S1:
      // wait if I'm blocked
      while(gatekeeper == 1);

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

      // get time of last finished txn
      tx->ts_cache = last_complete.val;

      return true;
  }

  /**
   *  CohortsLazy commit (read-only):
   */
  void
  CohortsLazy::commit_ro()
  {
      TxThread* tx = stm::Self;
      // mark self status
      tx->status = COHORTS_COMMITTED;

      // clean up
      tx->r_orecs.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  CohortsLazy commit (writing context):
   *
   */
  void
  CohortsLazy::commit_rw()
  {
      TxThread* tx = stm::Self;
      // Mark a global flag, no one is allowed to begin now
      //
      // [mfs] If we used ADD on gatekeper, we wouldn't need to do a FAI on
      //       timestamp.val later
      gatekeeper = 1;

      // Mark self pending to commit
      tx->status = COHORTS_CPENDING;

      // Get an order
      tx->order = 1 + faiptr(&timestamp.val);

      // For later use, indicates if I'm the last tx in this cohort
      bool lastone = true;

      // Wait until all tx are ready to commit
      //
      // [mfs] Some key information is lost here.  If I am the first
      // transaction, then when I do this loop, I could easily figure out
      // exactly how many transactions are in the cohort.  If I then set that
      // value in a global, nobody else would later have to go searching around
      // to try to figure out if they are the oldest or not.
      for (uint32_t i = 0; i < threadcount.val; ++i)
          while (threads[i]->status == COHORTS_STARTED);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // If I'm the first one in this cohort, no validation, else validate
      if (tx->order != last_order)
          validate(tx);

      // mark orec, do write back
      foreach (WriteSet, i, tx->writes) {
          orec_t* o = get_orec(i->addr);
          o->v.all = tx->order;
          *i->addr = i->val;
      }
      CFENCE;
      // Mark self as done
      last_complete.val = tx->order;

      // Mark self status
      tx->status = COHORTS_COMMITTED;
      WBR;

      // Am I the last one?
      for (uint32_t i = 0; lastone != false && i < threadcount.val; ++i)
          lastone &= (threads[i]->status != COHORTS_CPENDING);

      // If I'm the last one, release gatekeeper lock
      if (lastone) {
          last_order = tx->order + 1;
          gatekeeper = 0;
      }

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsLazy read (read-only transaction)
   */
  void*
  CohortsLazy::read_ro(STM_READ_SIG(addr,))
  {
      TxThread* tx = stm::Self;
      // log orec
      tx->r_orecs.insert(get_orec(addr));
      return *addr;
  }

  /**
   *  CohortsLazy read (writing transaction)
   */
  void*
  CohortsLazy::read_rw(STM_READ_SIG(addr,mask))
  {
      TxThread* tx = stm::Self;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // log orec
      tx->r_orecs.insert(get_orec(addr));

      void* tmp = *addr;
      REDO_RAW_CLEANUP(tmp, found, log, mask);
      return tmp;
  }

  /**
   *  CohortsLazy write (read-only context): for first write
   */
  void
  CohortsLazy::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  CohortsLazy write (writing context)
   */
  void
  CohortsLazy::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsLazy unwinder:
   */
  void
  CohortsLazy::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();

      PostRollback(tx);
  }

  /**
   *  CohortsLazy in-flight irrevocability:
   */
  bool
  CohortsLazy::irrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsLazy Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsLazy validation for commit: check that all reads are valid
   */
  void
  CohortsLazy::validate(TxThread* tx)
  {
      // [mfs] use the luke trick on this loop
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // If orec changed, abort
          if (ivt > tx->ts_cache) {
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
                  gatekeeper = 0;
              }
              tx->tmabort();
          }
      }
  }

  /**
   *  Switch to CohortsLazy:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   */
  void
  CohortsLazy::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = timestamp.val;
      // when switching algs, mark all tx committed status
      for (uint32_t i = 0; i < threadcount.val; ++i) {
          threads[i]->status = COHORTS_COMMITTED;
      }
  }
}

namespace stm {
  /**
   *  CohortsLazy initialization
   */
  template<>
  void initTM<CohortsLazy>()
  {
      // set the name
      stms[CohortsLazy].name      = "CohortsLazy";
      // set the pointers
      stms[CohortsLazy].begin     = ::CohortsLazy::begin;
      stms[CohortsLazy].commit    = ::CohortsLazy::commit_ro;
      stms[CohortsLazy].read      = ::CohortsLazy::read_ro;
      stms[CohortsLazy].write     = ::CohortsLazy::write_ro;
      stms[CohortsLazy].rollback  = ::CohortsLazy::rollback;
      stms[CohortsLazy].irrevoc   = ::CohortsLazy::irrevoc;
      stms[CohortsLazy].switcher  = ::CohortsLazy::onSwitchTo;
      stms[CohortsLazy].privatization_safe = true;
  }
}

