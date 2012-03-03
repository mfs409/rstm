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
 *  OrecLazy Implementation:
 *
 *    This STM is similar to the commit-time locking variant of TinySTM.  It
 *    also resembles the "patient" STM published by Spear et al. at PPoPP 2009.
 *    The key difference deals with the way timestamps are managed.  This code
 *    uses the manner of timestamps described by Wang et al. in their CGO 2007
 *    paper.  More details can be found in the OrecEager implementation.
 */

#include "../profiling.hpp"
#include "../cm.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

using stm::TxThread;
using stm::get_orec;
using stm::WriteSetEntry;
using stm::OrecList;
using stm::WriteSet;
using stm::orec_t;
using stm::timestamp;
using stm::timestamp_max;
using stm::id_version_t;
using stm::Self;
using stm::OnFirstWrite;
using stm::OnReadWriteCommit;
using stm::OnReadOnlyCommit;
using stm::PreRollback;
using stm::PostRollback;

namespace {
  template <class CM>
  struct OrecLazy_Generic
  {
      static TM_FASTCALL bool begin();
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro();
      static TM_FASTCALL void commit_rw();

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,));
      static void Initialize(int id, const char* name);
  };

  void onSwitchTo();
  bool irrevoc();
  NOINLINE void validate();

  template <class CM>
  void
  OrecLazy_Generic<CM>::Initialize(int id, const char* name)
  {
      // set the name
      stm::stms[id].name      = name;

      // set the pointers
      stm::stms[id].begin     = OrecLazy_Generic<CM>::begin;
      stm::stms[id].commit    = OrecLazy_Generic<CM>::commit_ro;
      stm::stms[id].read      = OrecLazy_Generic<CM>::read_ro;
      stm::stms[id].write     = OrecLazy_Generic<CM>::write_ro;
      stm::stms[id].rollback  = OrecLazy_Generic<CM>::rollback;
      stm::stms[id].irrevoc   = irrevoc;
      stm::stms[id].switcher  = onSwitchTo;
      stm::stms[id].privatization_safe = false;
  }

  /**
   *  OrecLazy begin:
   *
   *    Sample the timestamp and prepare local vars
   */
  template <class CM>
  bool
  OrecLazy_Generic<CM>::begin()
  {
      Self.allocator.onTxBegin();
      Self.start_time = timestamp.val;
      CM::onBegin();
      return false;
  }

  /**
   *  OrecLazy commit (read-only context)
   *
   *    We just reset local fields and we're done
   */
  template <class CM>
  void
  OrecLazy_Generic<CM>::commit_ro()
  {
      // notify CM
      CM::onCommit();
      // read-only
      Self.r_orecs.reset();
      OnReadOnlyCommit();
  }

  /**
   *  OrecLazy commit (writing context)
   *
   *    Using Wang-style timestamps, we grab all locks, validate, writeback,
   *    increment the timestamp, and then release all locks.
   */
  template <class CM>
  void
  OrecLazy_Generic<CM>::commit_rw()
  {
      // acquire locks
      foreach (WriteSet, i, Self.writes) {
          // get orec, read its version#
          orec_t* o = get_orec(i->addr);
          uintptr_t ivt = o->v.all;

          // lock all orecs, unless already locked
          if (ivt <= Self.start_time) {
              // abort if cannot acquire
              if (!bcasptr(&o->v.all, ivt, Self.my_lock.all))
                  Self.tmabort();
              // save old version to o->p, remember that we hold the lock
              o->p = ivt;
              Self.locks.insert(o);
          }
          // else if we don't hold the lock abort
          else if (ivt != Self.my_lock.all) {
              Self.tmabort();
          }
      }

      // validate
      foreach (OrecList, i, Self.r_orecs) {
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > Self.start_time) && (ivt != Self.my_lock.all))
              Self.tmabort();
      }

      // run the redo log
      Self.writes.writeback();

      // increment the global timestamp, release locks
      uintptr_t end_time = 1 + faiptr(&timestamp.val);
      foreach (OrecList, i, Self.locks)
          (*i)->v.all = end_time;

      // notify CM
      CM::onCommit();

      // clean-up
      Self.r_orecs.reset();
      Self.writes.reset();
      Self.locks.reset();
      OnReadWriteCommit( read_ro, write_ro, commit_ro);
  }

  /**
   *  OrecLazy read (read-only context):
   *
   *    in the best case, we just read the value, check the timestamp, log the
   *    orec and return
   */
  template <class CM>
  void*
  OrecLazy_Generic<CM>::read_ro(STM_READ_SIG(addr,))
  {
      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the location
          void* tmp = *addr;
          CFENCE;
          //  check the orec.
          //  NB: with this variant of timestamp, we don't need prevalidation
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: new read to uncontended location
          if (ivt.all <= Self.start_time) {
              Self.r_orecs.insert(o);
              return tmp;
          }

          // if lock held, spin and retry
          if (ivt.fields.lock) {
              spin64();
              continue;
          }

          // scale timestamp if ivt is too new, then try again
          uintptr_t newts = timestamp.val;
          validate();
          Self.start_time = newts;
      }
  }

  /**
   *  OrecLazy read (writing context):
   *
   *    Just like read-only context, but must check the write set first
   */
  template <class CM>
  void*
  OrecLazy_Generic<CM>::read_rw(STM_READ_SIG(addr,mask))
  {
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = Self.writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse the ReadRO barrier, which is adequate here---reduces LOC
      void* val = read_ro( addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  OrecLazy write (read-only context):
   *
   *    Buffer the write, and switch to a writing context
   */
  template <class CM>
  void
  OrecLazy_Generic<CM>::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      // add to redo log
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite( read_rw, write_rw, commit_rw);
  }

  /**
   *  OrecLazy write (writing context):
   *
   *    Just buffer the write
   */
  template <class CM>
  void
  OrecLazy_Generic<CM>::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      // add to redo log
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  OrecLazy rollback:
   *
   *    Release any locks we acquired (if we aborted during a commit()
   *    operation), and then reset local lists.
   */
  template <class CM>
  stm::scope_t*
  OrecLazy_Generic<CM>::rollback(STM_ROLLBACK_SIG( except, len))
  {
      PreRollback();

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(Self.writes, except, len);

      // release the locks and restore version numbers
      foreach (OrecList, i, Self.locks)
          (*i)->v.all = (*i)->p;

      // notify CM
      CM::onAbort();

      // undo memory operations, reset lists
      Self.r_orecs.reset();
      Self.writes.reset();
      Self.locks.reset();
      return PostRollback( read_ro, write_ro, commit_ro);
  }

  /**
   *  OrecLazy in-flight irrevocability:
   *
   *    Either commit the transaction or return false.
   */
   bool
   irrevoc()
   {
       return false;
       // NB: In a prior release, we actually had a full OrecLazy commit
       //     here.  Any contributor who is interested in improving this code
       //     should note that such an approach is overkill: by the time this
       //     runs, there are no concurrent transactions, so in effect, all
       //     that is needed is to validate, writeback, and return true.
   }

  /**
   *  OrecLazy validation:
   *
   *    We only call this when in-flight, which means that we don't have any
   *    locks... This makes the code very simple, but it is still better to not
   *    inline it.
   */
  void
  validate() {
      foreach (OrecList, i, Self.r_orecs)
          // abort if orec locked, or if unlocked but timestamp too new
          if ((*i)->v.all > Self.start_time)
              Self.tmabort();
  }

  /**
   *  Switch to OrecLazy:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   */
  void
  onSwitchTo() {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
  }
}

// -----------------------------------------------------------------------------
// Register initialization as declaratively as possible.
// -----------------------------------------------------------------------------
#define FOREACH_ORECLAZY(MACRO)                 \
    MACRO(OrecLazy, HyperAggressiveCM)          \
    MACRO(OrecLazyHour, HourglassCM)            \
    MACRO(OrecLazyBackoff, BackoffCM)           \
    MACRO(OrecLazyHB, HourglassBackoffCM)

#define INIT_ORECLAZY(ID, CM)                       \
    template <>                                     \
    void initTM<ID>() {                             \
        OrecLazy_Generic<stm::CM>::Initialize(ID, #ID); \
    }

namespace stm {
  FOREACH_ORECLAZY(INIT_ORECLAZY)
}

