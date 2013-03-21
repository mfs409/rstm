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
 * This is like OrecEager, except that:
 * 1 - it is only for x86
 * 2 - it is only for 64bit
 * 3 - it assumes no self-abort
 * 4 - it assumes single chip
 */

/**
 *  OrecEager Implementation:
 *
 *    This STM is similar to LSA/TinySTM and to the algorithm published by
 *    Wang et al. at CGO 2007.  The algorithm uses a table of orecs, direct
 *    update, encounter time locking, and undo logs.
 *
 *    The principal difference is in how OrecEager handles the modification
 *    of orecs when a transaction aborts.  In Wang's algorithm, a thread at
 *    commit time will first validate, then increment the counter.  This
 *    allows for threads to skip prevalidation of orecs in their read
 *    functions... however, it necessitates good CM, because on abort, a
 *    transaction must run its undo log, then get a new timestamp, and then
 *    release all orecs at that new time.  In essence, the aborted
 *    transaction does "silent stores", and these stores can cause other
 *    transactions to abort.
 *
 *    In LSA/TinySTM, each orec includes an "incarnation number" in the low
 *    bits.  When a transaction aborts, it runs its undo log, then it
 *    releases all locks and bumps the incarnation number.  If this results
 *    in incarnation number wraparound, then the abort function must
 *    increment the timestamp in the orec being released.  If this timestamp
 *    is larger than the current max timestamp, the aborting transaction must
 *    also bump the timestamp.  This approach has a lot of corner cases, but
 *    it allows for the abort-on-conflict contention manager.
 *
 *    In our code, we skip the incarnation numbers, and simply say that when
 *    releasing locks after undo, we increment each, and we keep track of the
 *    max value written.  If the value is greater than the timestamp, then at
 *    the end of the abort code, we increment the timestamp.  A few simple
 *    invariants about time ensure correctness.
 */

#include "algs.hpp"

namespace stm
{
  TM_FASTCALL void OrecEagerAMD64Commit(TX_LONE_PARAMETER);
  TM_FASTCALL void* OrecEagerAMD64Read(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void OrecEagerAMD64Write(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));

  /**
   *  OrecEagerAMD64 validation:
   *
   *    Make sure that all orecs that we've read have timestamps older than
   *    our start time, unless we locked those orecs.  If we locked the orec,
   *    we did so when the time was smaller than our start time, so we're
   *    sure to be OK.
   */
  void OrecEagerAMD64Validate(TxThread* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
              tmabort();
      }
  }

  void OrecEagerAMD64Begin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // sample the timestamp and prepare local structures
      tx->allocator.onTxBegin();
      tx->start_time = tickp();
      __sync_add_and_fetch(&tx->start_time, 0);
  }

  /**
   *  OrecEagerAMD64 commit:
   *
   *    read-only transactions do no work
   *
   *    writers must increment the timestamp, maybe validate, and then
   *    release locks
   */
  void OrecEagerAMD64Commit(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // use the lockset size to identify if tx is read-only
      if (!tx->locks.size()) {
          tx->r_orecs.reset();
          OnROCommit(tx);
          return;
      }

      // increment the global timestamp
      uintptr_t end_time = tickp();

      // skip validation if nobody else committed since my last validation
      //if (end_time != (tx->start_time + 1)) {
      foreach (OrecList, i, tx->r_orecs) {
          // abort unless orec older than start or owned by me
          uintptr_t ivt = (*i)->v.all;
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
              tmabort();
      }
      //}

      // release locks
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = end_time;

      // reset lock list and undo log
      tx->locks.reset();
      tx->undo_log.reset();
      // reset read list, do common cleanup
      tx->r_orecs.reset();
      OnRWCommit(tx);
  }

  /**
   *  OrecEagerAMD64 read:
   *
   *    Must check orec twice, and may need to validate
   */
  void*
  OrecEagerAMD64Read(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // get the orec addr, then start loop to read a consistent value
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

          // re-read orec AFTER reading value
          CFENCE;
          uintptr_t ivt2 = o->v.all;

          // common case: new read to an unlocked, old location
          if ((ivt.all == ivt2) && (ivt.all <= tx->start_time)) {
              tx->r_orecs.insert(o);
              return tmp;
          }

          // abort if locked
          if (__builtin_expect(ivt.fields.lock, 0))
              tmabort();

          // [mfs] This code looks funny...
          // tmabort();

          // scale timestamp if ivt is too new, then try again
          uintptr_t newts = tickp();
          __sync_add_and_fetch(&newts, 0);
          OrecEagerAMD64Validate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrecEagerAMD64 write:
   *
   *    Lock the orec, log the old value, do the write
   */
  void OrecEagerAMD64Write(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // get the orec addr, then enter loop to get lock from a consistent state
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec version number
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: uncontended location... try to lock it, abort on fail
          if (ivt.all <= tx->start_time) {
              if (!bcasptr(&o->v.all, ivt.all, tx->my_lock.all))
                  tmabort();

              // save old value, log lock, do the write, and return
              o->p = ivt.all;
              tx->locks.insert(o);
              tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
              STM_DO_MASKED_WRITE(addr, val, mask);
              return;
          }

          // next best: I already have the lock... must log old value, because
          // many locations hash to the same orec.  The lock does not mean I
          // have undo logged *this* location
          if (ivt.all == tx->my_lock.all) {
              tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
              STM_DO_MASKED_WRITE(addr, val, mask);
              return;
          }

          // fail if lock held by someone else
          if (ivt.fields.lock)
              tmabort();

          // [mfs] This code looks funny...
          //tmabort();

          // unlocked but too new... scale forward and try again
          uintptr_t newts = tickp();
          OrecEagerAMD64Validate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrecEagerAMD64 rollback:
   *
   *    Run the redo log, possibly bump timestamp
   */
  void
  OrecEagerAMD64Rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      // common rollback code
      PreRollback(tx);

      // run the undo log
      STM_UNDO(tx->undo_log, except, len);

      // release the locks and bump version numbers by one... since we are
      // using tick, this is beautifully simple
      foreach (OrecList, j, tx->locks) {
          uintptr_t newver = (*j)->p + 1;
          (*j)->v.all = newver;
      }

      // reset all lists
      tx->r_orecs.reset();
      tx->undo_log.reset();
      tx->locks.reset();

      // common unwind code when no pointer switching
      PostRollback(tx);
  }

  /**
   *  OrecEagerAMD64 in-flight irrevocability:
   *
   *    Either commit the transaction or return false.  Note that we're
   *    already serial by the time this code runs.
   *
   *    NB: This doesn't Undo anything, so there's no need to protect the
   *        stack.
   */
  bool
  OrecEagerAMD64Irrevoc(TxThread* tx)
  {
      // NB: This code is probably more expensive than it needs to be...
      // assert(false && "Didn't update this yet!");
      // assume we're a writer, and increment the global timestamp

      // [mfs] TODO:
      //uintptr_t end_time = 1 + faiptr(&timestamp.val);
      uintptr_t end_time = tickp();

      // skip validation only if nobody else committed
      //if (end_time != (tx->start_time + 1))
          foreach (OrecList, i, tx->r_orecs) {
              // read this orec
              uintptr_t ivt = (*i)->v.all;
              // if unlocked and newer than start time, abort
              if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
                  return false;
          }

      // release locks
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = end_time;

      // clean up
      tx->r_orecs.reset();
      tx->undo_log.reset();
      tx->locks.reset();
      return true;
  }

  /**
   *  Switch to OrecEagerAMD64:
   *
   *    Switching to/from OrecEagerAMD64 is extremely dangerous... we won't
   *    be able to re-use the Orec table.
   *
   *    [mfs] Can we do anything about this?
   */
  void OrecEagerAMD64OnSwitchTo() { }
}

REGISTER_REGULAR_ALG(OrecEagerAMD64, "OrecEagerAMD64", false)

#ifdef STM_ONESHOT_ALG_OrecEagerAMD64
DECLARE_AS_ONESHOT(OrecEagerAMD64)
#endif
