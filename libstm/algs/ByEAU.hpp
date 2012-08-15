/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef BYEAU_HPP__
#define BYEAU_HPP__

/**
 *  ByEAU Implementations
 *
 *    ByEAU means ByteEager with support for remotely aborting other threads
 *    when a conflict is detected.  Our hope with this framework is to model
 *    the behavior of BEHTM systems (specifically, their requestor wins)
 *    contention management policy.
 *
 *    This STM is templated.  The file implements ByEAU and ByEAUFCM.  The
 *    suffix of the name indicates how contention is managed.
 *
 *    ByEAU uses "Aggressive" conflict management.  This models requestor wins
 *    exactly: when A detects a conflict with B, A aborts B.
 *
 *    ByEAUFCM assigns each transaction a timestamp at begin time.  This is
 *    overly expensive, especially since we use a single shared counter.  FCM
 *    then uses this time as in Bobba's ISCA 2007 paper to establish guidelines
 *    for who wins in any conflict.
 */

#include "../cm.hpp"
#include "algs.hpp"

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace stm
{
  template <class CM>
  TM_FASTCALL void* ByEAUGenericReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  template <class CM>
  TM_FASTCALL void* ByEAUGenericReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  template <class CM>
  TM_FASTCALL void ByEAUGenericWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  template <class CM>
  TM_FASTCALL void ByEAUGenericWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  template <class CM>
  TM_FASTCALL void ByEAUGenericCommitRO(TX_LONE_PARAMETER);
  template <class CM>
  TM_FASTCALL void ByEAUGenericCommitRW(TX_LONE_PARAMETER);

  /**
   *  ByEAUGeneric begin:
   */
  template <class CM>
  void ByEAUGenericBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // mark self alive
      tx->alive = TX_ACTIVE;
      // notify the CM
      CM::onBegin(tx);
      // NB: allocator call at end since CM may block
      tx->allocator.onTxBegin();
  }

  /**
   *  ByEAUGeneric commit (read-only):
   */
  template <class CM>
  TM_FASTCALL
  void ByEAUGenericCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // read-only... release read locks
      foreach (ByteLockList, i, tx->r_bytelocks)
          (*i)->reader[tx->id-1] = 0;

      // notify CM
      CM::onCommit(tx);

      // reset lists
      tx->r_bytelocks.reset();
      OnROCommit(tx);
  }

  /**
   *  ByEAUGeneric commit (writing context):
   *
   *    Since this is ByteEager, we just drop the locks to commit, regardless of
   *    the CM policy.
   */
  template <class CM>
  TM_FASTCALL
  void ByEAUGenericCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // release write locks, then read locks
      foreach (ByteLockList, i, tx->w_bytelocks)
          (*i)->owner = 0;
      foreach (ByteLockList, i, tx->r_bytelocks)
          (*i)->reader[tx->id-1] = 0;

      // notify CM
      CM::onCommit(tx);

      // clean-up
      tx->r_bytelocks.reset();
      tx->w_bytelocks.reset();
      tx->undo_log.reset();

      OnRWCommit(tx);
      ResetToRO(tx, ByEAUGenericReadRO<CM>, ByEAUGenericWriteRO<CM>,
                ByEAUGenericCommitRO<CM>);
  }

  /**
   *  ByEAUGeneric read (read-only transaction)
   */
  template <class CM>
  TM_FASTCALL
  void* ByEAUGenericReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      bytelock_t* lock = get_bytelock(addr);

      // If I don't have a read lock, get one
      if (lock->reader[tx->id-1] == 0) {
          // first time read, log this location
          tx->r_bytelocks.insert(lock);
          // mark my lock byte
          lock->set_read_byte(tx->id-1);
      }

      // abort the owner and wait until it cleans up
      while (uint32_t owner = lock->owner) {
          // only abort owner if CM says it's ok
          if (CM::mayKill(tx, owner - 1))
              threads[owner-1]->alive = TX_ABORTED;
          else
              tmabort();
          // NB: must have liveness check in the spin, since we may have read
          //     locks
          if (tx->alive == TX_ABORTED)
              tmabort();
      }

      // do the read
      CFENCE;
      void* result = *addr;
      CFENCE;

      // check for remote abort
      if (tx->alive == TX_ABORTED)
          tmabort();
      return result;
  }

  /**
   *  ByEAUGeneric read (writing transaction)
   */
  template <class CM>
  TM_FASTCALL
  void* ByEAUGenericReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      bytelock_t* lock = get_bytelock(addr);

      // skip instrumentation if I am the writer
      if (lock->owner != tx->id) {
          // make sure I have a read lock
          if (lock->reader[tx->id-1] == 0) {
              // first time read, log this location
              tx->r_bytelocks.insert(lock);
              // mark my lock byte
              lock->set_read_byte(tx->id-1);
          }

          // abort the owner and wait until it cleans up
          while (uint32_t owner = lock->owner) {
              if (CM::mayKill(tx, owner - 1))
                  threads[owner-1]->alive = TX_ABORTED;
              else
                  tmabort();
              // NB: again, need liveness check
              if (tx->alive == TX_ABORTED)
                  tmabort();
          }
      }

      // do the read
      CFENCE;
      void* result = *addr;
      CFENCE;

      // check for remote abort
      if (tx->alive == TX_ABORTED)
          tmabort();
      return result;
  }

  /**
   *  ByEAUGeneric write (read-only context)
   */
  template <class CM>
  TM_FASTCALL
  void ByEAUGenericWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      bytelock_t* lock = get_bytelock(addr);

      // abort current owner, wait for release, then acquire
      while (true) {
          // abort the owner if there is one
          if (uint32_t owner = lock->owner)
              // must get permission from CM, else abort self to prevent deadlock
              if (CM::mayKill(tx, owner - 1))
                  threads[owner-1]->alive = TX_ABORTED;
              else
                  tmabort();
          // try to get ownership
          else if (bcas32(&(lock->owner), 0u, tx->id))
              break;
          // liveness check
          if (tx->alive == TX_ABORTED)
              tmabort();
      }

      // log the lock, drop any read locks I have
      tx->w_bytelocks.insert(lock);
      lock->reader[tx->id-1] = 0;

      // abort active readers
      for (int i = 0; i < 60; ++i)
          if (lock->reader[i] != 0) {
              // again, only abort readers with CM permission, else abort self
              if (CM::mayKill(tx, i))
                  threads[i]->alive = TX_ABORTED;
              else
                  tmabort();
          }

      // add to undo log, do in-place write
      tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
      STM_DO_MASKED_WRITE(addr, val, mask);

      // check for remote abort
      if (tx->alive == TX_ABORTED)
          tmabort();

      OnFirstWrite(tx, ByEAUGenericReadRW<CM>, ByEAUGenericWriteRW<CM>,
                        ByEAUGenericCommitRW<CM>);
  }

  /**
   *  ByEAUGeneric write (writing context)
   */
  template <class CM>
  TM_FASTCALL
  void ByEAUGenericWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      bytelock_t* lock = get_bytelock(addr);

      // skip all this if I have the lock
      if (lock->owner != tx->id) {
          // abort current owner, wait for release, then acquire
          while (true) {
              // abort the owner if there is one
              if (uint32_t owner = lock->owner)
                  // need CM permission
                  if (CM::mayKill(tx, owner-1))
                      threads[owner-1]->alive = TX_ABORTED;
                  else
                      tmabort();
              // try to get ownership
              else if (bcas32(&(lock->owner), 0u, tx->id))
                  break;
              // liveness check
              if (tx->alive == TX_ABORTED)
                  tmabort();
          }
          // log the lock, drop any read locks I have
          tx->w_bytelocks.insert(lock);
          lock->reader[tx->id-1] = 0;

          // abort active readers
          for (int i = 0; i < 60; ++i)
              if (lock->reader[i] != 0) {
                  // get permission to abort reader
                  if (CM::mayKill(tx, i))
                      threads[i]->alive = TX_ABORTED;
                  else
                      tmabort();
              }
      }

      // add to undo log, do in-place write
      tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
      STM_DO_MASKED_WRITE(addr, val, mask);

      // check for remote abort
      if (tx->alive == TX_ABORTED)
          tmabort();
  }

  /**
   *  ByEAUGeneric unwinder:
   *
   *    All ByEAU algorithms unwind in the same way: we run the undo log, then we
   *    release locks, notify the CM, and clean up.
   */
  template <class CM>
  void ByEAUGenericRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Undo the writes, while at the same time watching out for the exception
      // object.
      STM_UNDO(tx->undo_log, except, len);

      // release write locks, then read locks
      foreach (ByteLockList, j, tx->w_bytelocks)
          (*j)->owner = 0;
      foreach (ByteLockList, j, tx->r_bytelocks)
          (*j)->reader[tx->id-1] = 0;

      // reset lists
      tx->r_bytelocks.reset();
      tx->w_bytelocks.reset();
      tx->undo_log.reset();

      CM::onAbort(tx);

      PostRollback(tx);
      ResetToRO(tx, ByEAUGenericReadRO<CM>, ByEAUGenericWriteRO<CM>,
                ByEAUGenericCommitRO<CM>);
  }

  /**
   *  ByEAUGeneric in-flight irrevocability:
   */
  template <class CM>
  bool
  ByEAUGenericIrrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  Switch to ByEAUGeneric:
   *
   *    No algorithm leaves the ByteLock array in a nonzero state, so we
   *    don't have any overhead here.
   */
  template <class CM>
  void
  ByEAUGenericOnSwitchTo() { }
}

#endif // BYEAU_HPP__
