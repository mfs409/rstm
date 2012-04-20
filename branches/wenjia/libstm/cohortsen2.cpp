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
 *  CohortsEN2 Implementation
 *
 *  CohortsEN2 is CohortsNorec with inplace write if I'm the last one in the
 *  cohort. (LOSE CONDITION TO GO TURBO.)
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

// define atomic operations
#define CAS __sync_val_compare_and_swap
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch

using stm::TxThread;
using stm::last_complete;
using stm::WriteSet;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::ValueList;
using stm::ValueListEntry;
using stm::started;
using stm::cpending;
using stm::committed;
using stm::last_order;
using stm::threads;
using stm::threadcount;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  const uintptr_t VALIDATION_FAILED = 1;
  volatile uint32_t inplace = 0;
  NOINLINE bool validate(TxThread* tx);

  struct CohortsEN2 {
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
  };

  /**
   *  CohortsEN2 begin:
   *  CohortsEN2 has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  bool
  CohortsEN2::begin(TxThread* tx)
  {
      tx->allocator.onTxBegin();

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

      return true;
  }

  /**
   *  CohortsEN2 commit (read-only):
   */
  void
  CohortsEN2::commit_ro(TxThread* tx)
  {
      // decrease total number of tx started
      SUB(&started.val, 1);

      // clean up
      tx->vlist.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  CohortsEN2 commit (in place write commit): no validation, no write back
   *  no other thread touches cpending
   */
  void
  CohortsEN2::commit_turbo(TxThread* tx)
  {
      // increase # of tx waiting to commit, and use it as the order
      tx->order = ADD(&cpending.val, 1);

      // clean up
      tx->vlist.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);

      // wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // reset in place write flag
      inplace = 0;

      // increase # of committed
      committed.val ++;
      CFENCE;

      // mark self as done
      last_complete.val = tx->order;
  }

  /**
   *  CohortsEN2 commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsEN2::commit_rw(TxThread* tx)
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
          if (!validate(tx)) {
              committed.val++;
              CFENCE;
              last_complete.val = tx->order;
              tx->tmabort(tx);
          }

      // do write back
      tx->writes.writeback();

      // update last_order
      last_order = started.val + 1;

      // increase total number of committed tx
      committed.val ++;
      CFENCE;

      // mark self as done
      last_complete.val = tx->order;

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsEN2 read_turbo
   */
  void*
  CohortsEN2::read_turbo(STM_READ_SIG(tx,addr,))
  {
      return *addr;
  }

  /**
   *  CohortsEN2 read (read-only transaction)
   */
  void*
  CohortsEN2::read_ro(STM_READ_SIG(tx,addr,))
  {
      void *tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsEN2 read (writing transaction)
   */
  void*
  CohortsEN2::read_rw(STM_READ_SIG(tx,addr,mask))
  {
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
   *  CohortsEN2 write (read-only context): for first write
   */
  void
  CohortsEN2::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // If everyone else is ready to commit, do in place write
      if (cpending.val + 1 == started.val) {
          // set up flag indicating in place write starts
          atomicswap32(&inplace, 1);
          // double check is necessary
          if (cpending.val + 1 == started.val) {
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
   *  CohortsEN2 write (in place write)
   */
  void
  CohortsEN2::write_turbo(STM_WRITE_SIG(tx,addr,val,mask))
  {
      *addr = val; // in place write
  }

  /**
   *  CohortsEN2 write (writing context)
   */
  void
  CohortsEN2::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // Every write we test if I can go turbo
      if (cpending.val + 1 == started.val) {
          // set up flag indicating in place write starts
          atomicswap32(&inplace, 1);
          // double check is necessary
          if (cpending.val + 1 == started.val) {
              // write previous writeset back
              tx->writes.writeback();
              // in place write
              *addr = val;
              // go turbo mode
              OnFirstWrite(tx, read_turbo, write_turbo, commit_turbo);
              return;
          }
          inplace = 0;
      }
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsEN2 unwinder:
   */
  stm::scope_t*
  CohortsEN2::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists
      tx->vlist.reset();
      tx->writes.reset();

      return PostRollback(tx);
  }

  /**
   *  CohortsEN2 in-flight irrevocability:
   */
  bool
  CohortsEN2::irrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsEN2 Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsEN2 validation for commit: check that all reads are valid
   */
  bool
  validate(TxThread* tx)
  {
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid) return false;
      }
      return true;
  }

  /**
   *  Switch to CohortsEN2:
   *
   */
  void
  CohortsEN2::onSwitchTo()
  {
      last_complete.val = 0;
      inplace = 0;
  }
}

namespace stm {
  /**
   *  CohortsEN2 initialization
   */
  template<>
  void initTM<CohortsEN2>()
  {
      // set the name
      stms[CohortsEN2].name      = "CohortsEN2";
      // set the pointers
      stms[CohortsEN2].begin     = ::CohortsEN2::begin;
      stms[CohortsEN2].commit    = ::CohortsEN2::commit_ro;
      stms[CohortsEN2].read      = ::CohortsEN2::read_ro;
      stms[CohortsEN2].write     = ::CohortsEN2::write_ro;
      stms[CohortsEN2].rollback  = ::CohortsEN2::rollback;
      stms[CohortsEN2].irrevoc   = ::CohortsEN2::irrevoc;
      stms[CohortsEN2].switcher  = ::CohortsEN2::onSwitchTo;
      stms[CohortsEN2].privatization_safe = true;
  }
}

