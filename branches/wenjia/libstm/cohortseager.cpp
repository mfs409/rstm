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
 *  CohortsEager Implementation
 *
 *  Similiar to Cohorts, except that if I'm the last one in the cohort, I
 *  go to turbo mode, do in place read and write, and do turbo commit.
 */

#include "profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

// define atomic operations
#define CAS __sync_val_compare_and_swap
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch

using stm::TxThread;
using stm::last_complete;
using stm::timestamp;
using stm::timestamp_max;
using stm::WriteSet;
using stm::OrecList;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::orec_t;
using stm::get_orec;

using stm::started;
using stm::cpending;
using stm::committed;
using stm::last_order;


/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {

  volatile uint32_t inplace = 0;

  struct CohortsEager {
      static TM_FASTCALL bool begin(TxThread*);
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_turbo(STM_READ_SIG(,,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_turbo(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void commit_ro(TxThread* tx);
      static TM_FASTCALL void commit_rw(TxThread* tx);
      static TM_FASTCALL void commit_turbo(TxThread* tx);

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE void validate(TxThread* tx);
  };

  /**
   *  CohortsEager begin:
   *  CohortsEager has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  bool
  CohortsEager::begin(TxThread* tx)
  {
    S1:
      // wait until everyone is committed
      while (cpending.val != committed.val);

      // before tx begins, increase total number of tx
      ADD(&started.val, 1);

      // [NB] we must double check no one is ready to commit yet
      // and no one entered in place write phase(turbo mode)
      if (cpending.val > committed.val || inplace == 1){
          SUB(&started.val, 1);
          goto S1;
      }

      tx->allocator.onTxBegin();
      // get time of last finished txn
      tx->ts_cache = last_complete.val;

      return true;
  }

  /**
   *  CohortsEager commit (read-only):
   */
  void
  CohortsEager::commit_ro(TxThread* tx)
  {
      // decrease total number of tx started
      SUB(&started.val, 1);

      // clean up
      tx->r_orecs.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  CohortsEager commit (in place write commit): no validation, no write back
   *  no other thread touches cpending
   */
  void
  CohortsEager::commit_turbo(TxThread* tx)
  {
      // increase # of tx waiting to commit, and use it as the order
      ADD(&cpending.val, 1);

      // clean up
      tx->r_orecs.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);

      // wait for my turn, in this case, cpending is my order
      while (last_complete.val != (uintptr_t)(cpending.val - 1));

      // reset in place write flag
      inplace = 0;

      // mark self as done
      last_complete.val = cpending.val;

      // increase # of committed
      ADD(&committed.val, 1);
  }

  /**
   *  CohortsEager commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsEager::commit_rw(TxThread* tx)
  {
      // increase # of tx waiting to commit, and use it as the order
      tx->order = ADD(&cpending.val ,1);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // Wait until all tx are ready to commit
      while (cpending.val < started.val);

      // If in place write occurred, all tx validate reads
      // Otherwise, only first one skips validation
      if (inplace == 1 || tx->order != last_order)
          validate(tx);

      // Last one doesn't needs to mark orec
      if ((uint32_t)tx->order != started.val)
          foreach (WriteSet, i, tx->writes) {
              // get orec
              orec_t* o = get_orec(i->addr);
              // mark orec
              o->v.all = tx->order;
              // do write back
              *i->addr = i->val;
          }
      else
          tx->writes.writeback();

      // increase total number of committed tx
      // [NB] Using atomic instruction might be faster
      ADD(&committed.val, 1);
      // committed.val ++;
      // WBR;

      // mark self as done
      last_complete.val = tx->order;

      // update last_order
      last_order = started.val + 1;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsEager read_turbo
   */
  void*
  CohortsEager::read_turbo(STM_READ_SIG(tx,addr,))
  {
      return *addr;
  }

  /**
   *  CohortsEager read (read-only transaction)
   */
  void*
  CohortsEager::read_ro(STM_READ_SIG(tx,addr,))
  {
      // log orec
      tx->r_orecs.insert(get_orec(addr));
      return *addr;
  }

  /**
   *  CohortsEager read (writing transaction)
   */
  void*
  CohortsEager::read_rw(STM_READ_SIG(tx,addr,mask))
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
   *  CohortsEager write (read-only context): for first write
   */
  void
  CohortsEager::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // If everyone else is ready to commit, do in place write
      if (cpending.val + 1 == started.val) {
          // set up flag indicating in place write starts
          // [NB]When testing on MacOS, better use CAS
          inplace = 1;
          WBR;
          // double check is necessary
          if (cpending.val + 1 == started.val) {
              // mark orec
              orec_t* o = get_orec(addr);
              o->v.all = started.val;
              // in place write
              *addr = val;
              // go turbo mode
              OnFirstWrite(tx, read_turbo, write_turbo, commit_turbo);
              return;
          }
          // reset flag
          inplace = 0;
      }
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  CohortsEager write (in place write)
   */
  void
  CohortsEager::write_turbo(STM_WRITE_SIG(tx,addr,val,mask))
  {
      orec_t* o = get_orec(addr);
      o->v.all = started.val; // mark orec
      *addr = val; // in place write
  }

  /**
   *  CohortsEager write (writing context)
   */
  void
  CohortsEager::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsEager unwinder:
   */
  stm::scope_t*
  CohortsEager::rollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsEager in-flight irrevocability:
   */
  bool
  CohortsEager::irrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsEager Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsEager validation for commit: check that all reads are valid
   */
  void
  CohortsEager::validate(TxThread* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // If orec changed , abort
          if (ivt > tx->ts_cache) {
              // increase total number of committed tx
              // ADD(&committed.val, 1);
              committed.val ++;
              WBR;
              // set self as completed
              last_complete.val = tx->order;
              // abort
              tx->tmabort(tx);
          }
      }
  }

  /**
   *  Switch to CohortsEager:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   */
  void
  CohortsEager::onSwitchTo()
  {
      last_complete.val = 0;
  }
}

namespace stm {
  /**
   *  CohortsEager initialization
   */
  template<>
  void initTM<CohortsEager>()
  {
      // set the name
      stms[CohortsEager].name      = "CohortsEager";
      // set the pointers
      stms[CohortsEager].begin     = ::CohortsEager::begin;
      stms[CohortsEager].commit    = ::CohortsEager::commit_ro;
      stms[CohortsEager].read      = ::CohortsEager::read_ro;
      stms[CohortsEager].write     = ::CohortsEager::write_ro;
      stms[CohortsEager].rollback  = ::CohortsEager::rollback;
      stms[CohortsEager].irrevoc   = ::CohortsEager::irrevoc;
      stms[CohortsEager].switcher  = ::CohortsEager::onSwitchTo;
      stms[CohortsEager].privatization_safe = true;
  }
}

