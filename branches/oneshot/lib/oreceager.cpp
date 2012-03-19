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

#if 0

#include "../profiling.hpp"
#include "../cm.hpp"
#include "algs.hpp"

using stm::TxThread;
using stm::timestamp;
using stm::OrecList;
using stm::orec_t;
using stm::get_orec;
using stm::id_version_t;
using stm::UndoLogEntry;


/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 *
 *  NB: OrecEager actually does better without fine-grained switching for
 *      read-only transactions, so we don't support the read-only optimization
 *      in this code.
 */
namespace {
  template <class CM>
  struct OrecEager_Generic
  {
      static TM_FASTCALL bool begin(TxThread*);
      static TM_FASTCALL void commit(TxThread*);
      static void initialize(int id, const char* name);
      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
  };

  TM_FASTCALL void* read(STM_READ_SIG(,,));
  TM_FASTCALL void write(STM_WRITE_SIG(,,,));
  bool irrevoc(TxThread*);
  NOINLINE void validate(TxThread*);
  void onSwitchTo();

  // -----------------------------------------------------------------------------
  // OrecEager implementation
  // -----------------------------------------------------------------------------
  template <class CM>
  void
  OrecEager_Generic<CM>::initialize(int id, const char* name)
  {
      // set the name
      stm::stms[id].name      = name;

      // set the pointers
      stm::stms[id].begin     = OrecEager_Generic<CM>::begin;
      stm::stms[id].commit    = OrecEager_Generic<CM>::commit;
      stm::stms[id].rollback  = OrecEager_Generic<CM>::rollback;

      stm::stms[id].read      = read;
      stm::stms[id].write     = write;
      stm::stms[id].irrevoc   = irrevoc;
      stm::stms[id].switcher  = onSwitchTo;
      stm::stms[id].privatization_safe = false;
  }

  template <class CM>
  bool
  OrecEager_Generic<CM>::begin(TxThread* tx)
  {
      // sample the timestamp and prepare local structures
      tx->allocator.onTxBegin();
      tx->start_time = timestamp.val;
      CM::onBegin(tx);
      return false;
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
  void
  OrecEager_Generic<CM>::commit(TxThread* tx)
  {
      // use the lockset size to identify if tx is read-only
      if (!tx->locks.size()) {
          CM::onCommit(tx);
          tx->r_orecs.reset();
          OnReadOnlyCommit(tx);
          return;
      }

      // increment the global timestamp
      uintptr_t end_time = 1 + faiptr(&timestamp.val);

      // skip validation if nobody else committed since my last validation
      if (end_time != (tx->start_time + 1)) {
          foreach (OrecList, i, tx->r_orecs) {
              // abort unless orec older than start or owned by me
              uintptr_t ivt = (*i)->v.all;
              if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
                  tx->tmabort(tx);
          }
      }

      // release locks
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = end_time;

      // notify CM
      CM::onCommit(tx);

      // reset lock list and undo log
      tx->locks.reset();
      tx->undo_log.reset();
      // reset read list, do common cleanup
      tx->r_orecs.reset();
      OnReadWriteCommit(tx);
  }

  /**
   *  OrecEager read:
   *
   *    Must check orec twice, and may need to validate
   */
  void*
  read(STM_READ_SIG(tx,addr,))
  {
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
              tx->tmabort(tx);

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
  void
  write(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // get the orec addr, then enter loop to get lock from a consistent state
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec version number
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: uncontended location... try to lock it, abort on fail
          if (ivt.all <= tx->start_time) {
              if (!bcasptr(&o->v.all, ivt.all, tx->my_lock.all))
                  tx->tmabort(tx);

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
              tx->tmabort(tx);

          // unlocked but too new... scale forward and try again
          uintptr_t newts = timestamp.val;
          validate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrecEager rollback:
   *
   *    Run the redo log, possibly bump timestamp
   */
  template <class CM>
  stm::scope_t*
  OrecEager_Generic<CM>::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      // common rollback code
      PreRollback(tx);

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
      tx->r_orecs.reset();
      tx->undo_log.reset();
      tx->locks.reset();

      // notify CM
      CM::onAbort(tx);

      // common unwind code when no pointer switching
      return PostRollback(tx);
  }

  /**
   *  OrecEager in-flight irrevocability:
   *
   *    Either commit the transaction or return false.  Note that we're already
   *    serial by the time this code runs.
   *
   *    NB: This doesn't Undo anything, so there's no need to protect the
   *        stack.
   */
  bool
  irrevoc(TxThread* tx)
  {
      // NB: This code is probably more expensive than it needs to be...

      // assume we're a writer, and increment the global timestamp
      uintptr_t end_time = 1 + faiptr(&timestamp.val);

      // skip validation only if nobody else committed
      if (end_time != (tx->start_time + 1)) {
          foreach (OrecList, i, tx->r_orecs) {
              // read this orec
              uintptr_t ivt = (*i)->v.all;
              // if unlocked and newer than start time, abort
              if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
                  return false;
          }
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
   *  OrecEager validation:
   *
   *    Make sure that all orecs that we've read have timestamps older than our
   *    start time, unless we locked those orecs.  If we locked the orec, we
   *    did so when the time was smaller than our start time, so we're sure to
   *    be OK.
   */
  void
  validate(TxThread* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
              tx->tmabort(tx);
      }
  }

  /**
   *  Switch to OrecEager:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   */
  void
  onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, stm::timestamp_max.val);
  }
} // (anonymous namespace)

// -----------------------------------------------------------------------------
// Register initialization as declaratively as possible.
// -----------------------------------------------------------------------------
#define FOREACH_ORECEAGER(MACRO)                \
    MACRO(OrecEager, HyperAggressiveCM)         \
    MACRO(OrecEagerHour, HourglassCM)           \
    MACRO(OrecEagerBackoff, BackoffCM)          \
    MACRO(OrecEagerHB, HourglassBackoffCM)

#define INIT_ORECEAGER(ID, CM)                          \
    template <>                                         \
    void initTM<ID>() {                                 \
        OrecEager_Generic<stm::CM>::initialize(ID, #ID);    \
    }

namespace stm {
  FOREACH_ORECEAGER(INIT_ORECEAGER)
}

#undef FOREACH_ORECEAGER
#undef INIT_ORECEAGER

#else

#include <iostream>
#include <setjmp.h>
#include "MiniVector.hpp"
#include "metadata.hpp"
#include "WBMMPolicy.hpp"
#include "Macros.hpp"
#include "UndoLog.hpp"

namespace stm
{
  typedef MiniVector<orec_t*>      OrecList;     // vector of orecs

  /**
   *  Store per-thread metadata.  There isn't much for CGL...
   */
  struct TX
  {
      /*** for flat nesting ***/
      int nesting_depth;

      /*** unique id for this thread ***/
      int id;

      /*** number of RO commits ***/
      int commits_ro;

      /*** number of RW commits ***/
      int commits_rw;

      id_version_t   my_lock;       // lock word for orec STMs

      int aborts;

      scope_t* volatile scope;      // used to roll back; also flag for isTxnl

      WBMMPolicy     allocator;     // buffer malloc/free
      uintptr_t      start_time;    // start time of transaction
      OrecList       r_orecs;       // read set for orec STMs
      OrecList       locks;         // list of all locks held by tx
      UndoLog        undo_log;      // etee undo log

      /*** constructor ***/
      TX();
  };

  /**
   *  Simple constructor for TX: zero all fields, get an ID
   */
  TX::TX() : nesting_depth(0), commits_ro(0), commits_rw(0), r_orecs(64), locks(64),
             aborts(0), scope(NULL), allocator(), undo_log(64)
  {
      id = faiptr(&threadcount.val);
      threads[id] = this;
      allocator.setID(id-1);
      // set up my lock word
      my_lock.fields.lock = 1;
      my_lock.fields.id = id;
  }

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
   *  For querying to get the current algorithm name
   */
  const char* tm_getalgname() { return "OrecEager"; }

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

  scope_t* rollback(TX* tx)
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


  void tm_begin(scope_t* scope)
  {
      TX* tx = Self;
      if (++tx->nesting_depth > 1)
          return;

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
  void tm_end()
  {
      TX* tx = Self;
      if (--tx->nesting_depth)
          return;

      // use the lockset size to identify if tx is read-only
      if (!tx->locks.size()) {
          tx->r_orecs.reset();
          tx->allocator.onTxCommit();
          ++tx->commits_ro;
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

#endif
