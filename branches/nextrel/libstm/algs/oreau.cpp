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
 *  OrEAU Implementation
 *
 *    This is OrecEager, with Aggressive contention management.  Whenever an
 *    in-flight transaction detects a conflict with another transaction, the
 *    detecting transaction causes the other transaction to abort.
 *
 *    NB: OrecEager does not benefit from _ro versions of functions.  Does
 *        This STM?
 */

#include "../profiling.hpp"
#include "../cm.hpp"
#include "algs.hpp"

using stm::TxThread;
using stm::timestamp;
using stm::timestamp_max;
using stm::OrecList;
using stm::orec_t;
using stm::get_orec;
using stm::id_version_t;
using stm::threads;
using stm::UndoLogEntry;
using stm::Self;
using stm::OnFirstWrite;
using stm::OnReadWriteCommit;
using stm::OnReadOnlyCommit;
using stm::PreRollback;
using stm::PostRollback;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  template <class CM>
  struct OrEAU_Generic
  {
      static void Initialize(int id, const char* name);
      static TM_FASTCALL bool begin();
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro();
      static TM_FASTCALL void commit_rw();

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,));
      static bool irrevoc();
      static void onSwitchTo();
      static NOINLINE void validate();
  };

  /**
   *  OrEAU initialization
   */
  template <class CM>
  void
  OrEAU_Generic<CM>::Initialize(int id, const char* name)
  {
      // set the name
      stm::stms[id].name      = name;

      // set the pointers
      stm::stms[id].begin     = OrEAU_Generic<CM>::begin;
      stm::stms[id].commit    = OrEAU_Generic<CM>::commit_ro;
      stm::stms[id].read      = OrEAU_Generic<CM>::read_ro;
      stm::stms[id].write     = OrEAU_Generic<CM>::write_ro;
      stm::stms[id].rollback  = OrEAU_Generic<CM>::rollback;
      stm::stms[id].irrevoc   = OrEAU_Generic<CM>::irrevoc;
      stm::stms[id].switcher  = OrEAU_Generic<CM>::onSwitchTo;
      stm::stms[id].privatization_safe = false;
  }

  /**
   *  OrEAU begin:
   */
  template <class CM>
  bool
  OrEAU_Generic<CM>::begin()
  {
      Self.allocator.onTxBegin();
      Self.start_time = timestamp.val;
      Self.alive = TX_ACTIVE;
      // notify CM
      CM::onBegin();
      return false;
  }

  /**
   *  OrEAU commit (read-only):
   */
  template <class CM>
  void
  OrEAU_Generic<CM>::commit_ro()
  {
      CM::onCommit();
      Self.r_orecs.reset();
      OnReadOnlyCommit();
  }

  /**
   *  OrEAU commit (writing context):
   */
  template <class CM>
  void
  OrEAU_Generic<CM>::commit_rw()
  {
      // we're a writer, so increment the global timestamp
      Self.end_time = 1 + faiptr(&timestamp.val);

      // skip validation if nobody else committed
      if (Self.end_time != (Self.start_time + 1)) {
          foreach (OrecList, i, Self.r_orecs) {
              // read this orec
              uintptr_t ivt = (*i)->v.all;
              // if unlocked and newer than start time, abort
              if ((ivt > Self.start_time) && (ivt != Self.my_lock.all))
                  Self.tmabort();
          }
      }

      // release locks
      foreach (OrecList, i, Self.locks)
          (*i)->v.all = Self.end_time;

      // notify CM
      CM::onCommit();

      // clean up
      Self.r_orecs.reset();
      Self.undo_log.reset();
      Self.locks.reset();
      OnReadWriteCommit( read_ro, write_ro, commit_ro);
  }

  /**
   *  OrEAU read (read-only transaction)
   */
  template <class CM>
  void*
  OrEAU_Generic<CM>::read_ro(STM_READ_SIG(addr,))
  {
      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec BEFORE we read anything else
          id_version_t ivt;
          ivt.all = o->v.all;
          CFENCE;

          // read the location
          void* tmp = *addr;

          // re-read orec
          CFENCE;
          uintptr_t ivt2 = o->v.all;

          // common case: new read to uncontended location
          if ((ivt.all == ivt2) && (ivt.all <= Self.start_time)) {
              Self.r_orecs.insert(o);
              return tmp;
          }

          // abort the owner if locked
          if (ivt.fields.lock) {
              if (CM::mayKill( ivt.fields.id - 1))
                  threads[ivt.fields.id-1]->alive = TX_ABORTED;
              else
                  Self.tmabort();
          }

          // liveness check
          if (Self.alive == TX_ABORTED)
              Self.tmabort();

          // scale timestamp if ivt2 is too new
          uintptr_t newts = timestamp.val;
          validate();
          Self.start_time = newts;
      }
  }

  /**
   *  OrEAU read (writing transaction)
   */
  template <class CM>
  void*
  OrEAU_Generic<CM>::read_rw(STM_READ_SIG(addr,))
  {
      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec BEFORE we read anything else
          id_version_t ivt;
          ivt.all = o->v.all;
          CFENCE;

          // read the location
          void* tmp = *addr;

          // best case: I locked it already
          if (ivt.all == Self.my_lock.all)
              return tmp;

          // re-read orec
          CFENCE;
          uintptr_t ivt2 = o->v.all;

          // common case: new read to uncontended location
          if ((ivt.all == ivt2) && (ivt.all <= Self.start_time)) {
              Self.r_orecs.insert(o);
              return tmp;
          }

          // abort the owner if locked
          if (ivt.fields.lock) {
              if (CM::mayKill( ivt.fields.id - 1))
                  threads[ivt.fields.id-1]->alive = TX_ABORTED;
              else
                  Self.tmabort();
          }

          // liveness check
          if (Self.alive == TX_ABORTED)
              Self.tmabort();

          // scale timestamp if ivt2 is too new
          uintptr_t newts = timestamp.val;
          validate();

          Self.start_time = newts;
      }
  }

  /**
   *  OrEAU write (read-only context)
   */
  template <class CM>
  void
  OrEAU_Generic<CM>::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec version number
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: uncontended location... lock it
          if (ivt.all <= Self.start_time) {
              if (!bcasptr(&o->v.all, ivt.all, Self.my_lock.all))
                  Self.tmabort();

              // save old, log lock, write, return
              o->p = ivt.all;
              Self.locks.insert(o);
              Self.undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
              STM_DO_MASKED_WRITE(addr, val, mask);
              OnFirstWrite( read_rw, write_rw, commit_rw);
              return;
          }

          // abort the owner if locked
          if (ivt.fields.lock) {
              if (CM::mayKill( ivt.fields.id - 1))
                  threads[ivt.fields.id-1]->alive = TX_ABORTED;
              else
                  Self.tmabort();
          }

          // liveness check
          if (Self.alive == TX_ABORTED)
              Self.tmabort();

          // unlocked but too new... scale forward and try again
          uintptr_t newts = timestamp.val;
          validate();
          Self.start_time = newts;
      }
  }

  /**
   *  OrEAU write (writing context)
   */
  template <class CM>
  void
  OrEAU_Generic<CM>::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec version number
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: uncontended location... lock it
          if (ivt.all <= Self.start_time) {
              if (!bcasptr(&o->v.all, ivt.all, Self.my_lock.all))
                  Self.tmabort();

              // save old, log lock, write, return
              o->p = ivt.all;
              Self.locks.insert(o);
              Self.undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
              STM_DO_MASKED_WRITE(addr, val, mask);
              return;
          }

          // next best: already have the lock
          if (ivt.all == Self.my_lock.all) {
              Self.undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
              STM_DO_MASKED_WRITE(addr, val, mask);
              return;
          }

          // abort owner if locked
          if (ivt.fields.lock) {
              if (CM::mayKill( ivt.fields.id - 1))
                  threads[ivt.fields.id-1]->alive = TX_ABORTED;
              else
                  Self.tmabort();
          }

          // liveness check
          if (Self.alive == TX_ABORTED)
              Self.tmabort();

          // unlocked but too new... scale forward and try again
          uintptr_t newts = timestamp.val;
          validate();
          Self.start_time = newts;
      }
  }

  /**
   *  OrEAU unwinder:
   */
  template <class CM>
  stm::scope_t*
  OrEAU_Generic<CM>::rollback(STM_ROLLBACK_SIG( except, len))
  {
      PreRollback();
      // run the undo log
      STM_UNDO(Self.undo_log, except, len);

      // release the locks and bump version numbers
      uintptr_t max = 0;
      // increment the version number of each held lock by one
      foreach (OrecList, j, Self.locks) {
          uintptr_t newver = (*j)->p + 1;
          (*j)->v.all = newver;
          max = (newver > max) ? newver : max;
      }
      // if we bumped a version number to higher than the timestamp, we
      // need to increment the timestamp or else this location could become
      // permanently unreadable
      uintptr_t ts = timestamp.val;
      if (max > ts)
          casptr(&timestamp.val, ts, ts+1);

      // notify CM
      CM::onAbort();

      // reset all lists
      Self.r_orecs.reset();
      Self.undo_log.reset();
      Self.locks.reset();

      return PostRollback( read_ro, write_ro, commit_ro);
  }

  /**
   *  OrEAU in-flight irrevocability:
   *
   *    Either commit the transaction or return false.  Note that we're already
   *    serial by the time this code runs.
   */
  template <class CM>
  bool
  OrEAU_Generic<CM>::irrevoc()
  {
      return false;
  }

  /**
   *  OrEAU validation
   *
   *    Make sure that during some time period where the seqlock is constant
   *    and odd, all values in the read log are still present in memory.
   */
  template <class CM>
  void
  OrEAU_Generic<CM>::validate()
  {
      foreach (OrecList, i, Self.r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > Self.start_time) && (ivt != Self.my_lock.all))
              Self.tmabort();
      }
  }

  /**
   *  Switch to OrEAU:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   */
  template <class CM>
  void
  OrEAU_Generic<CM>::onSwitchTo() {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
  }
}


// Register ByEAU initializer functions. Do this as declaratively as
// possible. Remember that they need to be in the stm:: namespace.
#define FOREACH_OREAU(MACRO)                    \
    MACRO(OrEAUBackoff, BackoffCM)                     \
    MACRO(OrEAUFCM, FCM)                        \
    MACRO(OrEAUNoBackoff, HyperAggressiveCM)           \
    MACRO(OrEAUHour, HourglassCM)

#define INIT_OREAU(ID, CM)                      \
    template <>                                 \
    void initTM<ID>() {                         \
        OrEAU_Generic<CM>::Initialize(ID, #ID); \
    }

namespace stm {
  FOREACH_OREAU(INIT_OREAU)
}

#undef FOREACH_OREAU
#undef INIT_OREAU
