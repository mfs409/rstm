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
 *  Cohortsnoorder Implementation
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
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

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
  struct Cohortsnoorder
  {
    static TM_FASTCALL bool begin(TxThread*);
    static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
    static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
    static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,,));
    static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,,));
    static TM_FASTCALL void commit_ro(TxThread*);
    static TM_FASTCALL void commit_rw(TxThread*);

    static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
    static bool irrevoc(TxThread*);
    static void onSwitchTo();
    static NOINLINE void validate(TxThread*);
    static NOINLINE void TxAbortWrapper(TxThread* tx);

  };

  /**
   *  Cohortsnoorder begin:
   *  At first, every tx can start, until one of the tx is ready to commit.
   *  Then no tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  bool
  Cohortsnoorder::begin(TxThread* tx)
  {
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
   *  Cohortsnoorder commit (read-only):
   */
  void
  Cohortsnoorder::commit_ro(TxThread* tx)
  {
    // decrease total number of tx
    SUB(&started.val, 1);

    // read-only, so just reset lists
    tx->r_orecs.reset();
    OnReadOnlyCommit(tx);
  }

  /**
   *  Cohortsnoorder commit (writing context):
   */
  void
  Cohortsnoorder::commit_rw(TxThread* tx)
  {
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
   *  Cohortsnoorder read (read-only transaction)
   */
  void*
  Cohortsnoorder::read_ro(STM_READ_SIG(tx,addr,))
  {
      // log orec
      tx->r_orecs.insert(get_orec(addr));
      return *addr;
  }

  /**
   *  Cohortsnoorder read (writing transaction)
   */
  void*
  Cohortsnoorder::read_rw(STM_READ_SIG(tx,addr,mask))
  {
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
   *  Cohortsnoorder write (read-only context)
   */
  void
  Cohortsnoorder::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  Cohortsnoorder write (writing context)
   */
  void
  Cohortsnoorder::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  Cohortsnoorder unwinder:
   */
  stm::scope_t*
  Cohortsnoorder::rollback(STM_ROLLBACK_SIG(tx, except, len))
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
      return PostRollback(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  Cohortsnoorder in-flight irrevocability:
   */
  bool
  Cohortsnoorder::irrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  Cohortsnoorder validation
   */
  void
  Cohortsnoorder::validate(TxThread* tx)
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
    Cohortsnoorder::TxAbortWrapper(TxThread* tx)
    {
      // Increase total number of committed tx
      ADD(&committed.val, 1);

      // abort
      tx->tmabort(tx);
    }

  /**
   *  Switch to Cohortsnoorder:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   */
  void
  Cohortsnoorder::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
  }
}

namespace stm {
  /**
   *  Cohortsnoorder initialization
   */
  template<>
  void initTM<Cohortsnoorder>()
  {
      // set the name
      stms[Cohortsnoorder].name      = "Cohortsnoorder";

      // set the pointers
      stms[Cohortsnoorder].begin     = ::Cohortsnoorder::begin;
      stms[Cohortsnoorder].commit    = ::Cohortsnoorder::commit_ro;
      stms[Cohortsnoorder].read      = ::Cohortsnoorder::read_ro;
      stms[Cohortsnoorder].write     = ::Cohortsnoorder::write_ro;
      stms[Cohortsnoorder].rollback  = ::Cohortsnoorder::rollback;
      stms[Cohortsnoorder].irrevoc   = ::Cohortsnoorder::irrevoc;
      stms[Cohortsnoorder].switcher  = ::Cohortsnoorder::onSwitchTo;
      stms[Cohortsnoorder].privatization_safe = false;
  }
}
