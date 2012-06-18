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

#include "profiling.hpp"
#include "cm.hpp"
#include "algs.hpp"

using stm::TxThread;
using stm::OrecList;
using stm::orec_t;
using stm::get_orec;
using stm::id_version_t;
using stm::UndoLogEntry;


/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 *
 *  NB: X86_OE_NC actually does better without fine-grained switching for
 *      read-only transactions, so we don't support the read-only optimization
 *      in this code.
 */
namespace
{
  template <class CM>
  struct X86_OE_NC_Generic
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

  template <class CM>
  void
  X86_OE_NC_Generic<CM>::initialize(int id, const char* name)
  {
      // set the name
      stm::stms[id].name      = name;

      // set the pointers
      stm::stms[id].begin     = X86_OE_NC_Generic<CM>::begin;
      stm::stms[id].commit    = X86_OE_NC_Generic<CM>::commit;
      stm::stms[id].rollback  = X86_OE_NC_Generic<CM>::rollback;

      stm::stms[id].read      = read;
      stm::stms[id].write     = write;
      stm::stms[id].irrevoc   = irrevoc;
      stm::stms[id].switcher  = onSwitchTo;
      stm::stms[id].privatization_safe = false;
  }

  template <class CM>
  bool
  X86_OE_NC_Generic<CM>::begin(TxThread* tx)
  {
      // sample the timestamp and prepare local structures
      tx->allocator.onTxBegin();
      tx->start_time = tick();
      CM::onBegin(tx);
      return false;
  }

  /**
   *  X86_OE_NC commit:
   *
   *    read-only transactions do no work
   *
   *    writers must increment the timestamp, maybe validate, and then release
   *    locks
   */
  template <class CM>
  void
  X86_OE_NC_Generic<CM>::commit(TxThread* tx)
  {
      // use the lockset size to identify if tx is read-only
      if (!tx->locks.size()) {
          CM::onCommit(tx);
          tx->r_orecs.reset();
          OnReadOnlyCommit(tx);
          return;
      }

      // increment the global timestamp
      uintptr_t end_time = tick();

      // skip validation if nobody else committed since my last validation
      //if (end_time != (tx->start_time + 1)) {
          foreach (OrecList, i, tx->r_orecs) {
              // abort unless orec older than start or owned by me
              uintptr_t ivt = (*i)->v.all;
              if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
                  tx->tmabort(tx);
          }
      //}

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
   *  X86_OE_NC read:
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

          tx->tmabort(tx);

          // scale timestamp if ivt is too new, then try again
          uintptr_t newts = tick();
          validate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  X86_OE_NC write:
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

          tx->tmabort(tx);

          // unlocked but too new... scale forward and try again
          uintptr_t newts = tick();
          validate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  X86_OE_NC rollback:
   *
   *    Run the redo log, possibly bump timestamp
   */
  template <class CM>
  stm::scope_t*
  X86_OE_NC_Generic<CM>::rollback(STM_ROLLBACK_SIG(tx, except, len))
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

      // notify CM
      CM::onAbort(tx);

      // common unwind code when no pointer switching
      return PostRollback(tx);
  }

  /**
   *  X86_OE_NC in-flight irrevocability:
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
      assert(false && "Didn't update this yet!");
      // assume we're a writer, and increment the global timestamp

      // [mfs] TODO:
      uintptr_t end_time = 1 ;//+ faiptr(&timestamp.val);

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
   *  X86_OE_NC validation:
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
   *  Switch to X86_OE_NC:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   */
  void
  onSwitchTo()
  {
      // timestamp.val = MAXIMUM(timestamp.val, stm::timestamp_max.val);
  }
} // (anonymous namespace)

// -----------------------------------------------------------------------------
// Register initialization as declaratively as possible.
// -----------------------------------------------------------------------------
/*
#define FOREACH_X86_OE_NC(MACRO)                \
    MACRO(X86_OE_NC, HyperAggressiveCM)         \
    MACRO(X86_OE_NCHour, HourglassCM)           \
    MACRO(X86_OE_NCBackoff, BackoffCM)          \
    MACRO(X86_OE_NCHB, HourglassBackoffCM)
*/
#define FOREACH_X86_OE_NC(MACRO)                \
    MACRO(X86_OE_NC, HyperAggressiveCM)

#define INIT_X86_OE_NC(ID, CM)                          \
    template <>                                         \
    void initTM<ID>() {                                 \
        X86_OE_NC_Generic<stm::CM>::initialize(ID, #ID);    \
    }

namespace stm {
  FOREACH_X86_OE_NC(INIT_X86_OE_NC)
}

#undef FOREACH_X86_OE_NC
#undef INIT_X86_OE_NC
