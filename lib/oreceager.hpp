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

#include <iostream>
#include <setjmp.h>
#include "MiniVector.hpp"
#include "metadata.hpp"
#include "WBMMPolicy.hpp"
#include "Macros.hpp"
#include "tx.hpp"

namespace stm
{
  // for CM
  pad_word_t fcm_timestamp = {0};
  pad_word_t epochs[MAX_THREADS] = {{0}};

  /**
   *  No system initialization is required, since the timestamp is already 0
   */
  void tm_sys_init() { }

  /**
   *  When the transactional system gets shut down, we call this to dump
   *  stats for all threads
   */
  void tm_sys_shutdown()
  {
      static volatile unsigned int mtx = 0;
      // while (!bcas32(&mtx, 0u, 1u)) { }
      for (uint32_t i = 0; i < threadcount.val; i++) {
          std::cout << "Thread: "       << threads[i]->id
                    << "; RO Commits: " << threads[i]->commits_ro
                    << "; RW Commits: " << threads[i]->commits_rw
                    << "; Aborts: "     << threads[i]->aborts
                    << std::endl;
      }
      CFENCE;
      mtx = 0;
  }

  /**
   *  To initialize the thread's TM support, we need only ensure it has a
   *  descriptor.
   */
  void tm_thread_init()
  {
      // multiple inits from one thread do not cause trouble
      if (Self) return;

      // create a TxThread and save it in thread-local storage
      Self = new TX();
  }

  /**
   *  When a thread is done using the TM, we don't need to do anything
   *  special.
   */
  void tm_thread_shutdown() { }

  /**
   *  OrecEager rollback:
   *
   *    Run the redo log, possibly bump timestamp
   */

  /*** The only metadata we need is a single global padded lock ***/
  pad_word_t timestamp = {0};

  template <class CM>
  __attribute__((always_inline))
  scope_t* rollback_generic(TX* tx)
  {
      ++tx->aborts;

      // run the undo log
      STM_UNDO(tx->undo_log, except, len);

      // release the locks and bump version numbers by one... track the highest
      // version number we write, in case it is greater than timestamp.val
      uintptr_t max = 0;
      foreach (OrecList, j, tx->locks) {
          uintptr_t newver = (*j)->p + 1;
          (*j)->v.all = newver;
          max = (newver > max) ? newver : max;
      }
      // if we bumped a version number to higher than the timestamp, we need to
      // increment the timestamp to preserve the invariant that the timestamp
      // val is >= all orecs' values when unlocked
      uintptr_t ts = timestamp.val;
      if (max > ts)
          casptr(&timestamp.val, ts, (ts+1)); // assumes no transient failures

      // reset all lists
      CM::onAbort(tx);
      tx->r_orecs.reset();
      tx->undo_log.reset();
      tx->locks.reset();

      tx->allocator.onTxAbort();
      tx->nesting_depth = 0;
      scope_t* scope = tx->scope;
      tx->scope = NULL;
      return scope;
  }

  /**
   *  Forward-declare rollback so that we can call it from tm_abort
   */
  scope_t* rollback(TX* tx);

  /**
   *  The default mechanism that libstm uses for an abort. An API environment
   *  may also provide its own abort mechanism (see itm2stm for an example of
   *  how the itm shim does this).
   *
   *  This is ugly because rollback has a configuration-dependent signature.
   */
  NOINLINE
  NORETURN
  void tm_abort(TX* tx)
  {
      jmp_buf* scope = (jmp_buf*)rollback(tx);
      // need to null out the scope
      longjmp(*scope, 1);
  }


  template <class CM>
  __attribute__((always_inline))
  void tm_begin_generic(scope_t* scope)
  {
      TX* tx = Self;
      if (++tx->nesting_depth > 1)
          return;

      CM::onBegin(tx);

      tx->scope = scope;
      // sample the timestamp and prepare local structures
      tx->allocator.onTxBegin();
      tx->start_time = timestamp.val;
  }

  NOINLINE
  void validate_commit(TX* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          // abort unless orec older than start or owned by me
          uintptr_t ivt = (*i)->v.all;
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
              tm_abort(tx);
      }
  }

  /**
   *  OrecEager validation:
   *
   *    Make sure that all orecs that we've read have timestamps older than our
   *    start time, unless we locked those orecs.  If we locked the orec, we
   *    did so when the time was smaller than our start time, so we're sure to
   *    be OK.
   */
  NOINLINE
  void validate(TX* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
              tm_abort(tx);
      }
  }

  /**
   *  OrecEager commit:
   *
   *    read-only transactions do no work
   *
   *    writers must increment the timestamp, maybe validate, and then release
   *    locks
   */
  template <class CM>
  __attribute__((always_inline))
  void tm_end_generic()
  {
      TX* tx = Self;
      if (--tx->nesting_depth)
          return;

      // use the lockset size to identify if tx is read-only
      if (!tx->locks.size()) {
          tx->r_orecs.reset();
          tx->allocator.onTxCommit();
          ++tx->commits_ro;
          CM::onCommit(tx);
          return;
      }

      // increment the global timestamp
      uintptr_t end_time = 1 + faiptr(&timestamp.val);

      // skip validation if nobody else committed since my last validation
      if (end_time != (tx->start_time + 1))
          validate_commit(tx);

      // release locks
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = end_time;

      // reset lock list and undo log
      CM::onCommit(tx);
      tx->locks.reset();
      tx->undo_log.reset();
      tx->r_orecs.reset();
      tx->allocator.onTxCommit();
      ++tx->commits_rw;
  }

  /**
   *  OrecEager read:
   *
   *    Must check orec twice, and may need to validate
   */
  void* tm_read(void** addr)
  {
      TX* tx = Self;

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
              tm_abort(tx);

          // scale timestamp if ivt is too new, then try again
          uintptr_t newts = timestamp.val;
          validate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrecEager write:
   *
   *    Lock the orec, log the old value, do the write
   */
  void tm_write(void** addr, void* val)
  {
      TX* tx = Self;

      // get the orec addr, then enter loop to get lock from a consistent state
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec version number
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: uncontended location... try to lock it, abort on fail
          if (ivt.all <= tx->start_time) {
              if (!bcasptr(&o->v.all, ivt.all, tx->my_lock.all))
                  tm_abort(tx);

              // save old value, log lock, do the write, and return
              o->p = ivt.all;
              tx->locks.insert(o);
              tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, ignored)));
              *addr = val;
              return;
          }

          // next best: I already have the lock... must log old value, because
          // many locations hash to the same orec.  The lock does not mean I
          // have undo logged *this* location
          if (ivt.all == tx->my_lock.all) {
              tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, ignored)));
              *addr = val;
              return;
          }

          // fail if lock held by someone else
          if (ivt.fields.lock)
              tm_abort(tx);

          // unlocked but too new... scale forward and try again
          uintptr_t newts = timestamp.val;
          validate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  get a chunk of memory that will be automatically reclaimed if the caller
   *  is a transaction that ultimately aborts
   */
  void* tm_alloc(size_t size) { return Self->allocator.txAlloc(size); }

  /**
   *  Free some memory.  If the caller is a transaction that ultimately aborts,
   *  the free will not happen.  If the caller is a transaction that commits,
   *  the free will happen at commit time.
   */
  void tm_free(void* p) { Self->allocator.txFree(p); }

  NOINLINE
  void UndoLog::undo()
  {
      for (iterator i = end() - 1, e = begin(); i >= e; --i)
          i->undo();
  }

} // (anonymous namespace)
