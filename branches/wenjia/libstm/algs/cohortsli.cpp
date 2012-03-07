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
 *  CohortsLI Implementation
 *
 *  CohortsLI has 4 stages. 1) Nobody is running. If anyone starts,
 *  goes to 2) Everybody is running. If anyone is ready to commit,
 *  goes to 3) Every rw tx gets an order, from now on, no one is
 *  allowed to start a tx anymore. When everyone in this cohort is
 *  ready to commit, goes to stage 4)Commit phase. Everyone commits
 *  in an order given in stage 3. When the last one finishes
 *  its commit, it goes to stage 1. Now tx is allowed to start again.
 */
#include "../profiling.hpp"
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
using stm::WriteSet;
using stm::OrecList;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::orec_t;
using stm::get_orec;
using stm::gatekeeper;
using stm::last_order;

volatile uint32_t in = 0;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  struct CohortsLI {
      static TM_FASTCALL bool begin(TxThread*);
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void commit_ro(TxThread* tx);
      static TM_FASTCALL void commit_rw(TxThread* tx);

      static TM_FASTCALL void commit_turbo(TxThread* tx);
      static TM_FASTCALL void* read_turbo(STM_READ_SIG(,,));
      static TM_FASTCALL void write_turbo(STM_WRITE_SIG(,,,));

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE void validate(TxThread* tx);
  };

  /**
   *  CohortsLI begin:
   *  CohortsLI has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  bool
  CohortsLI::begin(TxThread* tx)
  {
    S1:
      // wait if I'm blocked
      while(gatekeeper == 1);

      // set started
      tx->status = COHORTS_STARTED;
      WBR;

      // double check no one is ready to commit
      if (gatekeeper == 1 || in == 1){
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
   *  CohortsLI commit (read-only):
   */
  void
  CohortsLI::commit_ro(TxThread* tx)
  {
      // mark self status
      tx->status = COHORTS_COMMITTED;

      // clean up
      tx->r_orecs.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  CohortsLI commit_turbo (for write in place tx use):
   *
   */
  void
  CohortsLI::commit_turbo(TxThread* tx)
  {
      // Mark self pending to commit
      tx->status = COHORTS_CPENDING;

      // Turbo tx can clean up first
      tx->r_orecs.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // Reset inplace write flag
      in = 0;

      // Mark self as done
      last_complete.val = tx->order;

      // Mark self status
      tx->status = COHORTS_COMMITTED;

      // I must be the last one, so release gatekeeper lock
      last_order = tx->order + 1;
      gatekeeper = 0;
  }


  /**
   *  CohortsLI commit (writing context):
   *
   */
  void
  CohortsLI::commit_rw(TxThread* tx)
  {
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

      // If I'm the first one in this cohort and no inplace write happened
      // then, I will do no validation, else validate
      if (in == 1 || tx->order != last_order)
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
      for (uint32_t i = 0; i < threadcount.val; ++i)
          if (threads[i]->status == COHORTS_CPENDING) {
              lastone = false;
              break;
          }

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
   *  CohortsLI read (read-only transaction)
   */
  void*
  CohortsLI::read_ro(STM_READ_SIG(tx,addr,))
  {
      // log orec
      tx->r_orecs.insert(get_orec(addr));
      return *addr;
  }

  /**
   *  CohortsLI read_turbo (for write in place tx use)
   */
  void*
  CohortsLI::read_turbo(STM_READ_SIG(tx,addr,))
  {
      return *addr;
  }

  /**
   *  CohortsLI read (writing transaction)
   */
  void*
  CohortsLI::read_rw(STM_READ_SIG(tx,addr,mask))
  {
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
   *  CohortsLI write (read-only context): for first write
   */
  void
  CohortsLI::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
      uint32_t flag = 0;
      uint32_t i = 0;
      // check others' status
      for (i = 0; i < threadcount.val; ++i)
          if (threads[i]->status == COHORTS_STARTED)
              flag ++;

      // If every one else is ready to commit, do in place write, go turbo mode
      if (flag == 1) {
          // setup in place write flag, indicating inplace write is going to happen
          in = 1;
          WBR;
          // double check
          for (i = 0, flag = 0; i < threadcount.val; ++i)
              if (threads[i]->status == COHORTS_STARTED)
                  flag ++;
          if (flag == 1) {
              // Get an order now instead of in commit
              tx->order = 1 + faiptr(&timestamp.val);
              // do write back
              orec_t* o = get_orec(addr);
              o->v.all = tx->order;
              *addr = val;
              // go turbo
              OnFirstWrite(tx, read_turbo, write_turbo, commit_turbo);
              return;
          }
          // reset flag
          in = 0;
      }
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }
  /**
   *  CohortsLI write_turbo: for write in place tx
   */
  void
  CohortsLI::write_turbo(STM_WRITE_SIG(tx,addr,val,mask))
  {
      orec_t* o = get_orec(addr);
      o->v.all = tx->order;
      *addr = val;
  }


  /**
   *  CohortsLI write (writing context)
   */
  void
  CohortsLI::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsLI unwinder:
   */
  stm::scope_t*
  CohortsLI::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();

      return PostRollback(tx);
  }

  /**
   *  CohortsLI in-flight irrevocability:
   */
  bool
  CohortsLI::irrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsLI Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsLI validation for commit: check that all reads are valid
   */
  void
  CohortsLI::validate(TxThread* tx)
  {
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
              for (uint32_t i = 0; i < threadcount.val; ++i)
                  if (threads[i]->status == COHORTS_CPENDING) {
                      l = false;
                      break;
                  }
              // If I'm the last one, release gatekeeper lock
              if (l) {
                  last_order = tx->order + 1;
                  gatekeeper = 0;
              }
              tx->tmabort(tx);
          }
      }
  }

  /**
   *  Switch to CohortsLI:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   */
  void
  CohortsLI::onSwitchTo()
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
   *  CohortsLI initialization
   */
  template<>
  void initTM<CohortsLI>()
  {
      // set the name
      stms[CohortsLI].name      = "CohortsLI";
      // set the pointers
      stms[CohortsLI].begin     = ::CohortsLI::begin;
      stms[CohortsLI].commit    = ::CohortsLI::commit_ro;
      stms[CohortsLI].read      = ::CohortsLI::read_ro;
      stms[CohortsLI].write     = ::CohortsLI::write_ro;
      stms[CohortsLI].rollback  = ::CohortsLI::rollback;
      stms[CohortsLI].irrevoc   = ::CohortsLI::irrevoc;
      stms[CohortsLI].switcher  = ::CohortsLI::onSwitchTo;
      stms[CohortsLI].privatization_safe = true;
  }
}

