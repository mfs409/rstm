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
 *  CohortsNoorder Implementation
 *
 *  This algs is based on LLT, except that we add cohorts' properties.
 *  But unlike cohorts, we do not give orders at the beginning of any
 *  commits.
 *
 *  [mfs] It might be a good idea to add some internal adaptivity, so that we
 *        can use a simple write set (fixed size vector) when the number of
 *        writes is small, and only switch to the hashtable when the number of
 *        writes gets bigger.  Doing that could potentially make the code much
 *        faster for small transactions.
 *
 *  [mfs] Another question to consider is whether it would be a good idea to
 *        have the different threads take turns acquiring orecs... this would
 *        mean no parallel acquisition, but also no need for BCASPTR
 *        instructions.
 */

#include "../profiling.hpp"
#include "../algs.hpp"
#include "../RedoRAWUtils.hpp"

// define atomic operations
#define CAS __sync_val_compare_and_swap
#define ADD __sync_fetch_and_add
#define SUB __sync_fetch_and_sub

using stm::TxThread;
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
  struct CohortsNoorder
  {
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
    static NOINLINE void validate(TxThread*);
    static NOINLINE void TxAbortWrapper(TxThread* tx);

  };

  /**
   *  CohortsNoorder begin:
   *  At first, every tx can start, until one of the tx is ready to commit.
   *  Then no tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  bool
  CohortsNoorder::begin()
  {
      TxThread* tx = stm::Self;
    S1:
      // wait until everyone is committed
      while (cpending.val != committed.val);

      //before start, increase total number of tx in one cohort
      ADD(&started.val, 1);

      // [NB] we must double check no one is ready to commit yet!
      if (cpending.val > committed.val){
          SUB(&started.val, 1);
          goto S1;
      }

      // now start
      tx->allocator.onTxBegin();

      // get a start time
      tx->start_time = timestamp.val;

      return false;
  }

  /**
   *  CohortsNoorder commit (read-only):
   */
  void
  CohortsNoorder::commit_ro()
  {
      TxThread* tx = stm::Self;
    // decrease total number of tx
    SUB(&started.val, 1);

    // read-only, so just reset lists
    tx->r_orecs.reset();
    OnReadOnlyCommit(tx);
  }

  /**
   *  CohortsNoorder commit (writing context):
   */
  void
  CohortsNoorder::commit_rw()
  {
      TxThread* tx = stm::Self;
      // increase # of tx waiting to commit
      ADD(&cpending.val, 1);

      // Wait until every tx is ready to commit
      while (cpending.val < started.val);

      foreach (WriteSet, i, tx->writes) {
          // get orec, read its version#
          orec_t* o = get_orec(i->addr);
          uintptr_t ivt = o->v.all;
          // lock all orecs, unless already locked
          if (ivt <= tx->start_time) {
              // abort if cannot acquire
              if (!bcasptr(&o->v.all, ivt, tx->my_lock.all))
                  TxAbortWrapper(tx);
              // save old version to o->p, remember that we hold the lock
              o->p = ivt;
              tx->locks.insert(o);
          }
          // else if we don't hold the lock abort
          else if (ivt != tx->my_lock.all) {
              TxAbortWrapper(tx);
          }
      }

      // increment the global timestamp since we have writes
      uintptr_t end_time = 1 + faiptr(&timestamp.val);

      // skip validation if nobody else committed
      if (end_time != (tx->start_time + 1))
          validate(tx);

      // write back
      tx->writes.writeback();

      // release locks
      CFENCE;
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = end_time;

      // clean-up
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);

      // increase total number of committed tx
      ADD(&committed.val, 1);
  }

  /**
   *  CohortsNoorder read (read-only transaction)
   */
  void*
  CohortsNoorder::read_ro(STM_READ_SIG(addr,))
  {
      TxThread* tx = stm::Self;
      // log orec
      tx->r_orecs.insert(get_orec(addr));
      return *addr;
  }

  /**
   *  CohortsNoorder read (writing transaction)
   */
  void*
  CohortsNoorder::read_rw(STM_READ_SIG(addr,mask))
  {
      TxThread* tx = stm::Self;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // log orec
      tx->r_orecs.insert(get_orec(addr));

      void* tmp = *addr;
      // fixup is here to minimize the postvalidation orec read latency
      REDO_RAW_CLEANUP(tmp, found, log, mask);
      return tmp;
  }

  /**
   *  CohortsNoorder write (read-only context)
   */
  void
  CohortsNoorder::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  CohortsNoorder write (writing context)
   */
  void
  CohortsNoorder::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsNoorder unwinder:
   */
  void
  CohortsNoorder::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // release the locks and restore version numbers
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = (*i)->p;

      // undo memory operations, reset lists
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      PostRollback(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsNoorder in-flight irrevocability:
   */
  bool
  CohortsNoorder::irrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  CohortsNoorder validation
   */
  void
  CohortsNoorder::validate(TxThread* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
              TxAbortWrapper(tx);
      }
  }

  /**
   *   Cohorts Tx Abort Wrapper
   */
    void
    CohortsNoorder::TxAbortWrapper(TxThread* tx)
    {
      // Increase total number of committed tx
      ADD(&committed.val, 1);

      // abort
      tx->tmabort(tx);
    }

  /**
   *  Switch to CohortsNoorder:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   */
  void
  CohortsNoorder::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
  }
}

namespace stm {
  /**
   *  CohortsNoorder initialization
   */
  template<>
  void initTM<CohortsNoorder>()
  {
      // set the name
      stms[CohortsNoorder].name      = "CohortsNoorder";

      // set the pointers
      stms[CohortsNoorder].begin     = ::CohortsNoorder::begin;
      stms[CohortsNoorder].commit    = ::CohortsNoorder::commit_ro;
      stms[CohortsNoorder].read      = ::CohortsNoorder::read_ro;
      stms[CohortsNoorder].write     = ::CohortsNoorder::write_ro;
      stms[CohortsNoorder].rollback  = ::CohortsNoorder::rollback;
      stms[CohortsNoorder].irrevoc   = ::CohortsNoorder::irrevoc;
      stms[CohortsNoorder].switcher  = ::CohortsNoorder::onSwitchTo;
      stms[CohortsNoorder].privatization_safe = false;
  }
}
