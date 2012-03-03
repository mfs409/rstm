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
 *  OrecALA Implementation
 *
 *    This is similar to the Detlefs algorithm for privatization-safe STM,
 *    TL2-IP, and [Marathe et al. ICPP 2008].  We use commit time ordering to
 *    ensure that there are no delayed cleanup problems, and we poll the
 *    timestamp variable to address doomed transactions.  By using TL2-style
 *    timestamps, we also achieve ALA publication safety
 */
#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

using stm::TxThread;
using stm::timestamp;
using stm::timestamp_max;
using stm::last_complete;
using stm::WriteSet;
using stm::OrecList;
using stm::orec_t;
using stm::get_orec;
using stm::WriteSetEntry;
using stm::UNRECOVERABLE;
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
  struct OrecALA {
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
      static NOINLINE void privtest(uintptr_t ts);
  };

  /**
   *  OrecALA begin:
   *
   *    We need a starting point for the transaction.  If an in-flight
   *    transaction is committed, but still doing writeback, we can either start
   *    at the point where that transaction had not yet committed, or else we can
   *    wait for it to finish writeback.  In this code, we choose the former
   *    option.
   *
   *    NB: the latter option might be better, since there is no timestamp
   *        scaling
   */
  bool
  OrecALA::begin()
  {
      Self.allocator.onTxBegin();
      // Start after the last cleanup, instead of after the last commit, to
      // avoid spinning in begin()
      Self.start_time = last_complete.val;
      Self.ts_cache = Self.start_time;
      Self.end_time = 0;
      return false;
  }

  /**
   *  OrecALA commit (read-only):
   *
   *    RO commit is trivial
   */
  void
  OrecALA::commit_ro()
  {
      Self.r_orecs.reset();
      OnReadOnlyCommit();
  }

  /**
   *  OrecALA commit (writing context):
   *
   *    OrecALA commit is like LLT: we get the locks, increment the counter, and
   *    then validate and do writeback.  As in other systems, some increments
   *    lead to skipping validation.
   *
   *    After writeback, we use a second, trailing counter to know when all txns
   *    who incremented the counter before this tx are done with writeback.  Only
   *    then can this txn mark its writeback complete.
   */
  void
  OrecALA::commit_rw()
  {
      // acquire locks
      foreach (WriteSet, i, Self.writes) {
          // get orec, read its version#
          orec_t* o = get_orec(i->addr);
          uintptr_t ivt = o->v.all;

          // if orec not locked, lock it and save old to orec.p
          if (ivt <= Self.start_time) {
              // abort if cannot acquire
              if (!bcasptr(&o->v.all, ivt, Self.my_lock.all))
                  Self.tmabort();
              // save old version to o->p, remember that we hold the lock
              o->p = ivt;
              Self.locks.insert(o);
          }
          else if (ivt != Self.my_lock.all) {
              Self.tmabort();
          }
      }

      // increment the global timestamp
      Self.end_time = 1 + faiptr(&timestamp.val);

      // skip validation if nobody committed since my last validation
      if (Self.end_time != (Self.ts_cache + 1)) {
          foreach (OrecList, i, Self.r_orecs) {
              // read this orec
              uintptr_t ivt = (*i)->v.all;
              if ((ivt > Self.start_time) && (ivt != Self.my_lock.all))
                  Self.tmabort();
          }
      }

      // run the redo log
      Self.writes.writeback();

      // release locks
      CFENCE;
      foreach (OrecList, i, Self.locks)
          (*i)->v.all = Self.end_time;

      // now ensure that transactions depart from stm_end in the order that
      // they incremend the timestamp.  This avoids the "deferred update"
      // half of the privatization problem.
      while (last_complete.val != (Self.end_time - 1))
          spin64();
      last_complete.val = Self.end_time;

      // clean-up
      Self.r_orecs.reset();
      Self.writes.reset();
      Self.locks.reset();
      OnReadWriteCommit( read_ro, write_ro, commit_ro);
  }

  /**
   *  OrecALA read (read-only transaction)
   *
   *    Standard tl2-style read, but then we poll for potential privatization
   *    conflicts
   */
  void*
  OrecALA::read_ro(STM_READ_SIG(addr,))
  {
      // read the location, log the orec
      void* tmp = *addr;
      orec_t* o = get_orec(addr);
      Self.r_orecs.insert(o);
      CFENCE;

      // make sure this location isn't locked or too new
      if (o->v.all > Self.start_time)
          Self.tmabort();

      // privatization safety: poll the timestamp, maybe validate
      uintptr_t ts = timestamp.val;
      if (ts != Self.ts_cache)
          privtest( ts);
      // return the value we read
      return tmp;
  }

  /**
   *  OrecALA read (writing transaction)
   *
   *    Same as above, but with a writeset lookup.
   */
  void*
  OrecALA::read_rw(STM_READ_SIG(addr,mask))
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
   *  OrecALA write (read-only context)
   *
   *    Buffer the write, and switch to a writing context.
   */
  void
  OrecALA::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite( read_rw, write_rw, commit_rw);
  }

  /**
   *  OrecALA write (writing context)
   *
   *    Buffer the write
   */
  void
  OrecALA::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  OrecALA rollback:
   *
   *    This is a standard orec unwind function.  The only catch is that if a
   *    transaction aborted after incrementing the timestamp, it must wait its
   *    turn and then increment the trailing timestamp, to keep the two counters
   *    consistent.
   */
  stm::scope_t*
  OrecALA::rollback(STM_ROLLBACK_SIG( except, len))
  {
      PreRollback();

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(Self.writes, except, len);

      // release the locks and restore version numbers
      foreach (OrecList, i, Self.locks)
          (*i)->v.all = (*i)->p;
      Self.r_orecs.reset();
      Self.writes.reset();
      Self.locks.reset();

      // if we aborted after incrementing the timestamp, then we have to
      // participate in the global cleanup order to support our solution to
      // the deferred update half of the privatization problem.
      // NB:  Note that end_time is always zero for restarts and retrys
      if (Self.end_time != 0) {
          while (last_complete.val < (Self.end_time - 1))
              spin64();
          last_complete.val = Self.end_time;
      }
      return PostRollback( read_ro, write_ro, commit_ro);
  }

  /**
   *  OrecALA in-flight irrevocability:
   *
   *    Either commit the transaction or return false.  Note that we're already
   *    serial by the time this code runs.
   */
  bool
  OrecALA::irrevoc()
  {
      return false;
  }

  /**
   *  OrecALA validation
   *
   *    an in-flight transaction must make sure it isn't suffering from the
   *    "doomed transaction" half of the privatization problem.  We can get that
   *    effect by calling this after every transactional read.
   */
  void OrecALA::privtest(uintptr_t ts)
  {
      // optimized validation since we don't hold any locks
      foreach (OrecList, i, Self.r_orecs) {
          // if orec unlocked and newer than start time, it changed, so abort.
          // if locked, it's not locked by me so abort
          if ((*i)->v.all > Self.start_time)
              Self.tmabort();
      }

      // remember that we validated at this time
      Self.ts_cache = ts;
  }

  /**
   *  Switch to OrecALA:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   *    Also, last_complete must equal timestamp
   */
  void OrecALA::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = timestamp.val;
  }
}

namespace stm {
  /**
   *  OrecALA initialization
   */
  template<>
  void initTM<OrecALA>()
  {
      // set the name
      stm::stms[OrecALA].name     = "OrecALA";

      // set the pointers
      stm::stms[OrecALA].begin    = ::OrecALA::begin;
      stm::stms[OrecALA].commit   = ::OrecALA::commit_ro;
      stm::stms[OrecALA].read     = ::OrecALA::read_ro;
      stm::stms[OrecALA].write    = ::OrecALA::write_ro;
      stm::stms[OrecALA].rollback = ::OrecALA::rollback;
      stm::stms[OrecALA].irrevoc  = ::OrecALA::irrevoc;
      stm::stms[OrecALA].switcher = ::OrecALA::onSwitchTo;
      stm::stms[OrecALA].privatization_safe = true;
  }
}
