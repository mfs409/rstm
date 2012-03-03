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
 *  OrecFair Implementation
 *
 *    This STM is the reader-record variant of the Patient STM with starvation
 *    avoidance, from Spear et al. PPoPP 2009.
 *
 *    NB: this uses traditional TL2-style timestamps, instead of those from
 *        Wang et al. CGO 2007.
 *
 *    NB: This algorithm could cut a lot of latency if we made special
 *        versions of the read/write/commit functions to cover when the
 *        transaction does not have priority.
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

using stm::TxThread;
using stm::timestamp;
using stm::timestamp_max;
using stm::KARMA_FACTOR;
using stm::orec_t;
using stm::get_orec;
using stm::RREC_COUNT;
using stm::rrecs;
using stm::rrec_t;
using stm::get_rrec;
using stm::WriteSet;
using stm::OrecList;
using stm::RRecList;
using stm::id_version_t;
using stm::MAX_THREADS;
using stm::threads;
using stm::prioTxCount;
using stm::WriteSetEntry;
using stm::Self;
using stm::OnFirstWrite;
using stm::OnReadWriteCommit;
using stm::OnReadOnlyCommit;
using stm::PreRollback;
using stm::PostRollback;
using stm::exp_backoff;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace
{
  struct OrecFair {
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
      static NOINLINE void validate_committime();
  };

  /**
   *  OrecFair begin:
   *
   *    When a transaction aborts, it releases its priority.  Here we re-acquire
   *    priority.
   */
  bool
  OrecFair::begin()
  {
      Self.allocator.onTxBegin();
      Self.start_time = timestamp.val;
      // get priority
      long prio_bump = Self.consec_aborts / KARMA_FACTOR;
      if (prio_bump) {
          faiptr(&prioTxCount.val);
          Self.prio = prio_bump;
      }
      return false;
  }

  /**
   *  OrecFair commit (read-only):
   *
   *    Read-only commits are easy... we just make sure to give up any priority
   *    we have.
   */
  void
  OrecFair::commit_ro()
  {
      // If I had priority, release it
      if (Self.prio) {
          // decrease prio count
          faaptr(&prioTxCount.val, -1);

          // give up my priority
          Self.prio = 0;

          // clear metadata, reset list
          foreach (RRecList, i, Self.myRRecs)
              (*i)->unsetbit(Self.id-1);
          Self.myRRecs.reset();
      }
      Self.r_orecs.reset();
      OnReadOnlyCommit();
  }

  /**
   *  OrecFair commit (writing context):
   *
   *    This algorithm commits a transaction by first getting all locks, then
   *    checking if any lock conflicts with a higher-priority reader.  If there
   *    are no conflicts, then we commit, otherwise we self-abort.  Also, when
   *    acquiring locks, if we fail because a lower-priority transaction has the
   *    lock, we wait, because al writes are also reads, and thus we can simply
   *    wait for that thread to detect our conflict and abort itself.
   */
  void
  OrecFair::commit_rw()
  {
      // try to lock every location in the write set
      WriteSet::iterator i = Self.writes.begin(), e = Self.writes.end();
      while (i != e) {
          // get orec, read its version#
          orec_t* o = get_orec(i->addr);
          id_version_t ivt;
          ivt.all = o->v.all;

          // if orec not locked, lock it.  for simplicity, abort if timestamp
          // too new.
          if (ivt.all <= Self.start_time) {
              if (!bcasptr(&o->v.all, ivt.all, Self.my_lock.all)) {
                  spin64();
                  continue;
              }
              // save old version to o->p, log lock
              o->p = ivt.all;
              Self.locks.insert(o);
          }
          // else if we don't hold the lock abort
          else if (ivt.all != Self.my_lock.all) {
              if (!ivt.fields.lock)
                  Self.tmabort();
              // priority test... if I have priority, and the last unlocked
              // version of the orec was the one I read, and the current
              // owner has less priority than me, wait
              if (o->p <= Self.start_time) {
                  if (threads[ivt.fields.id-1]->prio < Self.prio) {
                      spin64();
                      continue;
                  }
              }
              Self.tmabort();
          }
          ++i;
      }

      // fail if our writes conflict with a higher priority txn's reads
      if (prioTxCount.val > 0) {
          // \exist prio txns.  accumulate read bits covering addresses in my
          // write set
          rrec_t accumulator = {{0}};
          foreach (WriteSet, j, Self.writes) {
              int index = (((uintptr_t)j->addr) >> 3) % RREC_COUNT;
              accumulator |= rrecs[index];
          }

          // check the accumulator for bits that represent higher-priority
          // transactions
          for (unsigned slot = 0; slot < MAX_THREADS; ++slot) {
              unsigned bucket = slot / rrec_t::BITS;
              unsigned mask = 1lu<<(slot % rrec_t::BITS);
              if (accumulator.bits[bucket] & mask) {
                  if (threads[slot]->prio > Self.prio)
                      Self.tmabort();
              }
          }
      }

      // increment the global timestamp if we have writes
      unsigned end_time = 1 + faiptr(&timestamp.val);

      // skip validation if nobody else committed
      if (end_time != (Self.start_time + 1))
          validate_committime();

      // run the redo log
      Self.writes.writeback();

      // NB: if we did the faa, then released writelocks, then released
      //     readlocks, we might be faster

      // If I had priority, release it
      if (Self.prio) {
          // decrease prio count
          faaptr(&prioTxCount.val, -1);

          // give up my priority
          Self.prio = 0;

          // clear metadata, reset list
          foreach (RRecList, j, Self.myRRecs)
              (*j)->unsetbit(Self.id-1);
          Self.myRRecs.reset();
      }

      // release locks
      foreach (OrecList, j, Self.locks)
          (*j)->v.all = end_time;

      // remember that this was a commit
      Self.r_orecs.reset();
      Self.writes.reset();
      Self.locks.reset();
      OnReadWriteCommit( read_ro, write_ro, commit_ro);
  }

  /**
   *  OrecFair read (read-only transaction)
   *
   *    This read is like OrecLazy, except that (1) we use traditional
   *    "check-twice" timestamps, and (2) if the caller has priority, it must
   *    mark the location before reading it.
   *
   *    NB: We could poll the 'set' bit first, which might afford some
   *        optimizations for priority transactions
   */
  void*
  OrecFair::read_ro(STM_READ_SIG(addr,))
  {
      // CM instrumentation
      if (Self.prio > 0) {
          // get the rrec for this address, set the bit, log it
          rrec_t* rrec = get_rrec(addr);
          rrec->setbit(Self.id-1);
          Self.myRRecs.insert(rrec);
      }

      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec BEFORE we read anything else
          id_version_t ivt;
          ivt.all = o->v.all;
          CFENCE;

          // read the location
          void* tmp = *addr;
          CFENCE;

          // re-read the orec
          unsigned ivt2 = o->v.all;

          // common case: new read to uncontended location
          if ((ivt.all == ivt2) && (ivt.all <= Self.start_time)) {
              Self.r_orecs.insert(o);
              return tmp;
          }

          // if lock held, spin and retry
          if (ivt.fields.lock) {
              yield_cpu();
              continue;
          }

          // unlocked but too new... validate and scale forward
          unsigned newts = timestamp.val;
          validate();
          Self.start_time = newts;
      }
  }

  /**
   *  OrecFair read (writing transaction)
   *
   *    This read is like OrecLazy, except that (1) we use traditional
   *    "check-twice" timestamps, and (2) if the caller has priority, it must
   *    mark the location before reading it.
   *
   *    NB: As above, we could poll the 'set' bit if we had a priority-only
   *        version of this function
   */
  void*
  OrecFair::read_rw(STM_READ_SIG(addr,mask))
  {
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = Self.writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // CM instrumentation
      if (Self.prio > 0) {
          // get the rrec for this address, set the bit, log it
          rrec_t* rrec = get_rrec(addr);
          rrec->setbit(Self.id-1);
          Self.myRRecs.insert(rrec);
      }

      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec BEFORE we read anything else
          id_version_t ivt;
          ivt.all = o->v.all;
          CFENCE;

          // read the location
          void* tmp = *addr;
          CFENCE;

          // re-read the orec
          unsigned ivt2 = o->v.all;

          // common case: new read to uncontended location
          if ((ivt.all == ivt2) && (ivt.all <= Self.start_time)) {
              Self.r_orecs.insert(o);
              // cleanup the value as late as possible.
              REDO_RAW_CLEANUP(tmp, found, log, mask);
              return tmp;
          }

          // if lock held, spin and retry
          if (ivt.fields.lock) {
              yield_cpu();
              continue;
          }

          // unlocked but too new... validate and scale forward
          unsigned newts = timestamp.val;
          validate();
          Self.start_time = newts;
      }
  }

  /**
   *  OrecFair write (read-only context)
   *
   *    Every write is also a read.  Doing so makes commis much faster.
   *    However, it also means that writes have much more overhead than
   *    OrecLazy, especially when we have priority.
   *
   *    NB: We could use the rrec to know when we don't have to check the
   *        timestamp and scale.  Also, it looks like this mechanism has some
   *        redundancy with the checks in the lock acquisition code.
   */
  void
  OrecFair::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      // CM instrumentation
      if (Self.prio > 0) {
          // get the rrec for this address, set the bit, log it
          rrec_t* rrec = get_rrec(addr);
          rrec->setbit(Self.id-1);
          Self.myRRecs.insert(rrec);
      }

      // ensure that the orec isn't newer than we are... if so, validate
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec version number
          id_version_t ivt;
          ivt.all = o->v.all;
          // if locked, spin and continue
          if (!ivt.fields.lock) {
              // do we need to scale the start time?
              if (ivt.all > Self.start_time) {
                  unsigned newts = timestamp.val;
                  validate();
                  Self.start_time = newts;
                  continue;
              }
              break;
          }
          spin64();
      }

      // Record the new value in a redo log
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite( read_rw, write_rw, commit_rw);
  }

  /**
   *  OrecFair write (writing context)
   *
   *    Same as the RO case, only without the switch at the end.  The same
   *    concerns apply as above.
   */
  void
  OrecFair::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      // CM instrumentation
      if (Self.prio > 0) {
          // get the rrec for this address, set the bit, log it
          rrec_t* rrec = get_rrec(addr);
          rrec->setbit(Self.id-1);
          Self.myRRecs.insert(rrec);
      }

      // ensure that the orec isn't newer than we are... if so, validate
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec version number
          id_version_t ivt;
          ivt.all = o->v.all;
          // if locked, spin and continue
          if (!ivt.fields.lock) {
              // do we need to scale the start time?
              if (ivt.all > Self.start_time) {
                  unsigned newts = timestamp.val;
                  validate();
                  Self.start_time = newts;
                  continue;
              }
              break;
          }
          spin64();
      }

      // Record the new value in a redo log
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  OrecFair unwinder:
   *
   *    To unwind, any rrecs or orecs that are marked must be unmarked.
   *
   *    NB: Unlike most of our algorithms, there is baked-in exponential
   *        backoff in this function, rather than deferring such backoff to a
   *        templated contention manager.  That is because we are trying to
   *        be completely faithful to [Spear PPoPP 2009]
   */
  stm::scope_t*
  OrecFair::rollback(STM_ROLLBACK_SIG( except, len))
  {
      PreRollback();

      // Perform writes to the exception object if there were any... taking
      // the branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(Self.writes, except, len);

      // release the locks and restore version numbers
      foreach (OrecList, i, Self.locks)
          (*i)->v.all = (*i)->p;

      // If I had priority, release it
      if (Self.prio > 0) {
          // give up my priority, unset all my read bits
          faaptr(&prioTxCount.val, -1);
          Self.prio = 0;
          foreach (RRecList, i, Self.myRRecs)
              (*i)->unsetbit(Self.id-1);
          Self.myRRecs.reset();
      }

      // undo memory operations, reset lists
      Self.r_orecs.reset();
      Self.writes.reset();
      Self.locks.reset();

      // randomized exponential backoff
      exp_backoff();

      return PostRollback( read_ro, write_ro, commit_ro);
  }

  /**
   *  OrecFair in-flight irrevocability: use abort-and-restart
   */
  bool
  OrecFair::irrevoc()
  {
      return false;
  }

  /**
   *  OrecFair validation
   *
   *    This is a lot like regular Orec validation, except that we must be
   *    ready for the possibility that someone with low priority grabbed a
   *    lock that we have an RRec on, in which case we just wait for them to
   *    go away, instead of aborting.
   */
  void
  OrecFair::validate()
  {
      OrecList::iterator i = Self.r_orecs.begin(), e = Self.r_orecs.end();
      while (i != e) {
          // read this orec
          id_version_t ivt;
          ivt.all = (*i)->v.all;
          // only a problem if locked or newer than start time
          if (ivt.all > Self.start_time) {
              if (!ivt.fields.lock)
                  Self.tmabort();
              // priority test... if I have priority, and the last unlocked
              // orec was the one I read, and the current owner has less
              // priority than me, wait
              if ((*i)->p <= Self.start_time) {
                  if (threads[ivt.fields.id-1]->prio < Self.prio) {
                      spin64();
                      continue;
                  }
              }
              Self.tmabort();
          }
          ++i;
      }
  }

  /**
   *  OrecFair validation (commit time)
   *
   *    This is a lot like the above code, except we need to handle when the
   *    caller holds locks
   */
  void
  OrecFair::validate_committime()
  {
      if (Self.prio) {
        OrecList::iterator i = Self.r_orecs.begin(), e = Self.r_orecs.end();
        while (i != e) {
              // read this orec
              id_version_t ivt;
              ivt.all = (*i)->v.all;
              // if unlocked and newer than start time, abort
              if (!ivt.fields.lock && (ivt.all > Self.start_time))
                  Self.tmabort();

              // if locked and not by me, do a priority test
              if (ivt.fields.lock && (ivt.all != Self.my_lock.all)) {
                  // priority test... if I have priority, and the last
                  // unlocked orec was the one I read, and the current
                  // owner has less priority than me, wait
                  if (((*i)->p <= Self.start_time) &&
                      (threads[ivt.fields.id-1]->prio < Self.prio))
                  {
                      spin64();
                      continue;
                  }
                  Self.tmabort();
              }
              ++i;
          }
      }
      else {
          foreach (OrecList, i, Self.r_orecs) {
              // read this orec
              id_version_t ivt;
              ivt.all = (*i)->v.all;
              // if unlocked and newer than start time, abort
              if ((ivt.all > Self.start_time) && (ivt.all != Self.my_lock.all))
                  Self.tmabort();
          }
      }
  }

  /**
   *  Switch to OrecFair:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   */
  void
  OrecFair::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
  }
}

namespace stm {

  /**
   *  OrecFair initialization
   */
  template<>
  void initTM<OrecFair>()
  {
      // set the name
      stm::stms[OrecFair].name      = "OrecFair";

      // set the pointers
      stm::stms[OrecFair].begin     = ::OrecFair::begin;
      stm::stms[OrecFair].commit    = ::OrecFair::commit_ro;
      stm::stms[OrecFair].read      = ::OrecFair::read_ro;
      stm::stms[OrecFair].write     = ::OrecFair::write_ro;
      stm::stms[OrecFair].rollback  = ::OrecFair::rollback;
      stm::stms[OrecFair].irrevoc   = ::OrecFair::irrevoc;
      stm::stms[OrecFair].switcher  = ::OrecFair::onSwitchTo;
      stm::stms[OrecFair].privatization_safe = false;
  }
}
