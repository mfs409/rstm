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
 *  OrecELA Implementation
 *
 *    This is similar to the Detlefs algorithm for privatization-safe STM,
 *    TL2-IP, and [Marathe et al. ICPP 2008].  We use commit time ordering to
 *    ensure that there are no delayed cleanup problems, we poll the timestamp
 *    variable to address doomed transactions, but unlike the above works, we
 *    use TinySTM-style extendable timestamps instead of TL2-style timestamps,
 *    which sacrifices some publication safety.
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

using stm::TxThread;
using stm::timestamp;
using stm::timestamp_max;
using stm::last_complete;
using stm::orec_t;
using stm::get_orec;
using stm::WriteSet;
using stm::OrecList;
using stm::WriteSetEntry;
using stm::id_version_t;
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
  struct OrecELA {
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
   *  OrecELA begin:
   *
   *    We need a starting point for the transaction.  If an in-flight
   *    transaction is committed, but still doing writeback, we can either start
   *    at the point where that transaction had not yet committed, or else we can
   *    wait for it to finish writeback.  In this code, we choose the former
   *    option.
   */
  bool
  OrecELA::begin()
  {
      Self.allocator.onTxBegin();
      // Start after the last cleanup, instead of after the last commit, to
      // avoid spinning in begin()
      Self.start_time = last_complete.val;
      Self.end_time = 0;
      return false;
  }

  /**
   *  OrecELA commit (read-only):
   *
   *    RO commit is trivial
   */
  void
  OrecELA::commit_ro()
  {
      Self.r_orecs.reset();
      OnReadOnlyCommit();
  }

  /**
   *  OrecELA commit (writing context):
   *
   *    OrecELA commit is like LLT: we get the locks, increment the counter, and
   *    then validate and do writeback.  As in other systems, some increments
   *    lead to skipping validation.
   *
   *    After writeback, we use a second, trailing counter to know when all txns
   *    who incremented the counter before this tx are done with writeback.  Only
   *    then can this txn mark its writeback complete.
   */
  void
  OrecELA::commit_rw()
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
              // save old version to o->p, log lock
              o->p = ivt;
              Self.locks.insert(o);
          }
          // else if we don't hold the lock abort
          else if (ivt != Self.my_lock.all) {
              Self.tmabort();
          }
      }

      // increment the global timestamp if we have writes
      Self.end_time = 1 + faiptr(&timestamp.val);

      // skip validation if possible
      if (Self.end_time != (Self.start_time + 1)) {
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
   *  OrecELA read (read-only transaction)
   *
   *    This is a traditional orec read for systems with extendable timestamps.
   *    However, we also poll the timestamp counter and validate any time a new
   *    transaction has committed, in order to catch doomed transactions.
   */
  void*
  OrecELA::read_ro(STM_READ_SIG(addr,))
  {
      // get the orec addr, read the orec's version#
      orec_t* o = get_orec(addr);
      while (true) {
          // read the location
          void* tmp = *addr;
          CFENCE;
          // check the orec.  Note: we don't need prevalidation because we
          // have a global clean state via the last_complete.val field.
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: new read to uncontended location
          if (ivt.all <= Self.start_time) {
              Self.r_orecs.insert(o);
              // privatization safety: avoid the "doomed transaction" half
              // of the privatization problem by polling a global and
              // validating if necessary
              uintptr_t ts = timestamp.val;
              if (ts != Self.start_time)
                  privtest( ts);
              return tmp;
          }

          // if lock held, spin and retry
          if (ivt.fields.lock) {
              spin64();
              continue;
          }

          // unlocked but too new... validate and scale forward
          uintptr_t newts = timestamp.val;
          foreach (OrecList, i, Self.r_orecs) {
              // if orec locked or newer than start time, abort
              if ((*i)->v.all > Self.start_time)
                  Self.tmabort();
          }

          uintptr_t cs = last_complete.val;
          // need to pick cs or newts
          Self.start_time = (newts < cs) ? newts : cs;
      }
  }

  /**
   *  OrecELA read (writing transaction)
   *
   *    Identical to RO case, but with write-set lookup first
   */
  void*
  OrecELA::read_rw(STM_READ_SIG(addr,mask))
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
   *  OrecELA write (read-only context)
   *
   *    Simply buffer the write and switch to a writing context
   */
  void
  OrecELA::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite( read_rw, write_rw, commit_rw);
  }

  /**
   *  OrecELA write (writing context)
   *
   *    Simply buffer the write
   */
  void
  OrecELA::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  OrecELA unwinder:
   *
   *    This is a standard orec unwind function.  The only catch is that if a
   *    transaction aborted after incrementing the timestamp, it must wait its
   *    turn and then increment the trailing timestamp, to keep the two counters
   *    consistent.
   */
  stm::scope_t*
  OrecELA::rollback(STM_ROLLBACK_SIG( except, len))
  {
      PreRollback();

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(Self.writes, except, len);

      // release locks and restore version numbers
      foreach (OrecList, i, Self.locks)
          (*i)->v.all = (*i)->p;
      Self.r_orecs.reset();
      Self.writes.reset();
      Self.locks.reset();

      // if we aborted after incrementing the timestamp, then we have to
      // participate in the global cleanup order to support our solution to
      // the deferred update half of the privatization problem.
      //
      // NB:  Note that end_time is always zero for restarts and retrys
      if (Self.end_time != 0) {
          while (last_complete.val < (Self.end_time - 1))
              spin64();
          last_complete.val = Self.end_time;
      }
      return PostRollback( read_ro, write_ro, commit_ro);
  }

  /**
   *  OrecELA in-flight irrevocability: use abort-and-restart
   */
  bool
  OrecELA::irrevoc()
  {
      return false;
  }

  /**
   *  OrecELA validation
   *
   *    an in-flight transaction must make sure it isn't suffering from the
   *    "doomed transaction" half of the privatization problem.  We can get that
   *    effect by calling this after every transactional read (actually every
   *    read that detects that some new transaction has committed).
   */
  void
  OrecELA::privtest(uintptr_t ts)
  {
      // optimized validation since we don't hold any locks
      foreach (OrecList, i, Self.r_orecs) {
          // if orec locked or newer than start time, abort
          if ((*i)->v.all > Self.start_time)
              Self.tmabort();
      }
      // careful here: we can't scale the start time past last_complete.val,
      // unless we want to re-introduce the need for prevalidation on every
      // read.
      uintptr_t cs = last_complete.val;
      Self.start_time = (ts < cs) ? ts : cs;
  }

  /**
   *  Switch to OrecELA:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   *    Also, last_complete must equal timestamp
   */
  void
  OrecELA::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = timestamp.val;
  }
}

namespace stm {
  /**
   *  OrecELA initialization
   */
  template<>
  void initTM<OrecELA>()
  {
      // set the name
      stm::stms[OrecELA].name     = "OrecELA";

      // set the pointers
      stm::stms[OrecELA].begin    = ::OrecELA::begin;
      stm::stms[OrecELA].commit   = ::OrecELA::commit_ro;
      stm::stms[OrecELA].read     = ::OrecELA::read_ro;
      stm::stms[OrecELA].write    = ::OrecELA::write_ro;
      stm::stms[OrecELA].rollback = ::OrecELA::rollback;
      stm::stms[OrecELA].irrevoc  = ::OrecELA::irrevoc;
      stm::stms[OrecELA].switcher = ::OrecELA::onSwitchTo;
      stm::stms[OrecELA].privatization_safe = true;
  }
}
