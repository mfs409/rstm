/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef OREAU_HPP__
#define OREAU_HPP__

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
#include "../algs.hpp"

using stm::TxThread;
using stm::timestamp;
using stm::timestamp_max;
using stm::OrecList;
using stm::orec_t;
using stm::get_orec;
using stm::id_version_t;
using stm::threads;
using stm::UndoLogEntry;


/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  template <class CM>
  struct OrEAU_Generic
  {
      static void initialize(int id, const char* name);
      static void begin(TX_LONE_PARAMETER);
      static TM_FASTCALL void* read_ro(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro(TX_LONE_PARAMETER);
      static TM_FASTCALL void commit_rw(TX_LONE_PARAMETER);

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE void validate(TxThread*);
  };

  /**
   *  OrEAU initialization
   */
  template <class CM>
  void
  OrEAU_Generic<CM>::initialize(int id, const char* name)
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
  void OrEAU_Generic<CM>::begin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
      tx->start_time = timestamp.val;
      tx->alive = TX_ACTIVE;
      // notify CM
      CM::onBegin(tx);
  }

  /**
   *  OrEAU commit (read-only):
   */
  template <class CM>
  void
  OrEAU_Generic<CM>::commit_ro(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      CM::onCommit(tx);
      tx->r_orecs.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  OrEAU commit (writing context):
   */
  template <class CM>
  void
  OrEAU_Generic<CM>::commit_rw(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // we're a writer, so increment the global timestamp
      tx->end_time = 1 + faiptr(&timestamp.val);

      // skip validation if nobody else committed
      if (tx->end_time != (tx->start_time + 1)) {
          foreach (OrecList, i, tx->r_orecs) {
              // read this orec
              uintptr_t ivt = (*i)->v.all;
              // if unlocked and newer than start time, abort
              if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
                  tx->tmabort();
          }
      }

      // release locks
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = tx->end_time;

      // notify CM
      CM::onCommit(tx);

      // clean up
      tx->r_orecs.reset();
      tx->undo_log.reset();
      tx->locks.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  OrEAU read (read-only transaction)
   */
  template <class CM>
  void*
  OrEAU_Generic<CM>::read_ro(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
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
          if ((ivt.all == ivt2) && (ivt.all <= tx->start_time)) {
              tx->r_orecs.insert(o);
              return tmp;
          }

          // abort the owner if locked
          if (ivt.fields.lock) {
              if (CM::mayKill(tx, ivt.fields.id - 1))
                  threads[ivt.fields.id-1]->alive = TX_ABORTED;
              else
                  tx->tmabort();
          }

          // liveness check
          if (tx->alive == TX_ABORTED)
              tx->tmabort();

          // scale timestamp if ivt2 is too new
          uintptr_t newts = timestamp.val;
          validate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrEAU read (writing transaction)
   */
  template <class CM>
  void*
  OrEAU_Generic<CM>::read_rw(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
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
          if (ivt.all == tx->my_lock.all)
              return tmp;

          // re-read orec
          CFENCE;
          uintptr_t ivt2 = o->v.all;

          // common case: new read to uncontended location
          if ((ivt.all == ivt2) && (ivt.all <= tx->start_time)) {
              tx->r_orecs.insert(o);
              return tmp;
          }

          // abort the owner if locked
          if (ivt.fields.lock) {
              if (CM::mayKill(tx, ivt.fields.id - 1))
                  threads[ivt.fields.id-1]->alive = TX_ABORTED;
              else
                  tx->tmabort();
          }

          // liveness check
          if (tx->alive == TX_ABORTED)
              tx->tmabort();

          // scale timestamp if ivt2 is too new
          uintptr_t newts = timestamp.val;
          validate(tx);

          tx->start_time = newts;
      }
  }

  /**
   *  OrEAU write (read-only context)
   */
  template <class CM>
  void
  OrEAU_Generic<CM>::write_ro(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec version number
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: uncontended location... lock it
          if (ivt.all <= tx->start_time) {
              if (!bcasptr(&o->v.all, ivt.all, tx->my_lock.all))
                  tx->tmabort();

              // save old, log lock, write, return
              o->p = ivt.all;
              tx->locks.insert(o);
              tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
              STM_DO_MASKED_WRITE(addr, val, mask);
              stm::OnFirstWrite(tx, read_rw, write_rw, commit_rw);
              return;
          }

          // abort the owner if locked
          if (ivt.fields.lock) {
              if (CM::mayKill(tx, ivt.fields.id - 1))
                  threads[ivt.fields.id-1]->alive = TX_ABORTED;
              else
                  tx->tmabort();
          }

          // liveness check
          if (tx->alive == TX_ABORTED)
              tx->tmabort();

          // unlocked but too new... scale forward and try again
          uintptr_t newts = timestamp.val;
          validate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrEAU write (writing context)
   */
  template <class CM>
  void
  OrEAU_Generic<CM>::write_rw(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec version number
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: uncontended location... lock it
          if (ivt.all <= tx->start_time) {
              if (!bcasptr(&o->v.all, ivt.all, tx->my_lock.all))
                  tx->tmabort();

              // save old, log lock, write, return
              o->p = ivt.all;
              tx->locks.insert(o);
              tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
              STM_DO_MASKED_WRITE(addr, val, mask);
              return;
          }

          // next best: already have the lock
          if (ivt.all == tx->my_lock.all) {
              tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
              STM_DO_MASKED_WRITE(addr, val, mask);
              return;
          }

          // abort owner if locked
          if (ivt.fields.lock) {
              if (CM::mayKill(tx, ivt.fields.id - 1))
                  threads[ivt.fields.id-1]->alive = TX_ABORTED;
              else
                  tx->tmabort();
          }

          // liveness check
          if (tx->alive == TX_ABORTED)
              tx->tmabort();

          // unlocked but too new... scale forward and try again
          uintptr_t newts = timestamp.val;
          validate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrEAU unwinder:
   */
  template <class CM>
  void
  OrEAU_Generic<CM>::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);
      // run the undo log
      STM_UNDO(tx->undo_log, except, len);

      // release the locks and bump version numbers
      uintptr_t max = 0;
      // increment the version number of each held lock by one
      foreach (OrecList, j, tx->locks) {
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
      CM::onAbort(tx);

      // reset all lists
      tx->r_orecs.reset();
      tx->undo_log.reset();
      tx->locks.reset();

      PostRollback(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  OrEAU in-flight irrevocability:
   *
   *    Either commit the transaction or return false.  Note that we're already
   *    serial by the time this code runs.
   */
  template <class CM>
  bool
  OrEAU_Generic<CM>::irrevoc(TxThread*)
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
  OrEAU_Generic<CM>::validate(TxThread* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
              tx->tmabort();
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

#endif // OREAU_HPP__
