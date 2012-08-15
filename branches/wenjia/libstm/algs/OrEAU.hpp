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


#include "../cm.hpp"
#include "algs.hpp"

namespace stm
{
  template <class CM>
  TM_FASTCALL void* OrEAUGenericReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  template <class CM>
  TM_FASTCALL void* OrEAUGenericReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  template <class CM>
  TM_FASTCALL void OrEAUGenericWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  template <class CM>
  TM_FASTCALL void OrEAUGenericWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  template <class CM>
  TM_FASTCALL void OrEAUGenericCommitRO(TX_LONE_PARAMETER);
  template <class CM>
  TM_FASTCALL void OrEAUGenericCommitRW(TX_LONE_PARAMETER);
  template <class CM>
  NOINLINE void OrEAUGenericValidate(TxThread*);

  /**
   *  OrEAU begin:
   */
  template <class CM>
  void OrEAUGenericBegin(TX_LONE_PARAMETER)
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
  TM_FASTCALL
  void
  OrEAUGenericCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      CM::onCommit(tx);
      tx->r_orecs.reset();
      OnROCommit(tx);
  }

  /**
   *  OrEAU commit (writing context):
   */
  template <class CM>
  TM_FASTCALL
  void
  OrEAUGenericCommitRW(TX_LONE_PARAMETER)
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
                  tmabort();
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
      OnRWCommit(tx);
      ResetToRO(tx, OrEAUGenericReadRO<CM>, OrEAUGenericWriteRO<CM>, OrEAUGenericCommitRO<CM>);
  }

  /**
   *  OrEAU read (read-only transaction)
   */
  template <class CM>
  TM_FASTCALL
  void*
  OrEAUGenericReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
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
                  tmabort();
          }

          // liveness check
          if (tx->alive == TX_ABORTED)
              tmabort();

          // scale timestamp if ivt2 is too new
          uintptr_t newts = timestamp.val;
          OrEAUGenericValidate<CM>(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrEAU read (writing transaction)
   */
  template <class CM>
  TM_FASTCALL
  void*
  OrEAUGenericReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
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
                  tmabort();
          }

          // liveness check
          if (tx->alive == TX_ABORTED)
              tmabort();

          // scale timestamp if ivt2 is too new
          uintptr_t newts = timestamp.val;
          OrEAUGenericValidate<CM>(tx);

          tx->start_time = newts;
      }
  }

  /**
   *  OrEAU write (read-only context)
   */
  template <class CM>
  TM_FASTCALL
  void
  OrEAUGenericWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
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
                  tmabort();

              // save old, log lock, write, return
              o->p = ivt.all;
              tx->locks.insert(o);
              tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
              STM_DO_MASKED_WRITE(addr, val, mask);
              OnFirstWrite(tx, OrEAUGenericReadRW<CM>, OrEAUGenericWriteRW<CM>, OrEAUGenericCommitRW<CM>);
              return;
          }

          // abort the owner if locked
          if (ivt.fields.lock) {
              if (CM::mayKill(tx, ivt.fields.id - 1))
                  threads[ivt.fields.id-1]->alive = TX_ABORTED;
              else
                  tmabort();
          }

          // liveness check
          if (tx->alive == TX_ABORTED)
              tmabort();

          // unlocked but too new... scale forward and try again
          uintptr_t newts = timestamp.val;
          OrEAUGenericValidate<CM>(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrEAU write (writing context)
   */
  template <class CM>
  TM_FASTCALL
  void
  OrEAUGenericWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
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
                  tmabort();

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
                  tmabort();
          }

          // liveness check
          if (tx->alive == TX_ABORTED)
              tmabort();

          // unlocked but too new... scale forward and try again
          uintptr_t newts = timestamp.val;
          OrEAUGenericValidate<CM>(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrEAU unwinder:
   */
  template <class CM>
  void OrEAUGenericRollback(STM_ROLLBACK_SIG(tx, except, len))
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

      PostRollback(tx);
      ResetToRO(tx, OrEAUGenericReadRO<CM>, OrEAUGenericWriteRO<CM>, OrEAUGenericCommitRO<CM>);
  }

  /**
   *  OrEAU in-flight irrevocability:
   *
   *    Either commit the transaction or return false.  Note that we're already
   *    serial by the time this code runs.
   */
  template <class CM>
  bool
  OrEAUGenericIrrevoc(TxThread*)
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
  OrEAUGenericValidate(TxThread* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
              tmabort();
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
  void OrEAUGenericOnSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
  }
}

#endif // OREAU_HPP__
