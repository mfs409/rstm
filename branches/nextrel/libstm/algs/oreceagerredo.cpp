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
 *  OrecEagerRedo Implementation
 *
 *    This code is very similar to the TinySTM-writeback algorithm.  It can
 *    also be thought of as OrecEager with redo logs instead of undo logs.
 *    Note, though, that it uses timestamps as in Wang's CGO 2007 paper, so
 *    we always validate at commit time but we don't have to check orecs
 *    twice during each read.
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

using stm::UNRECOVERABLE;
using stm::TxThread;
using stm::OrecList;
using stm::orec_t;
using stm::get_orec;
using stm::WriteSetEntry;
using stm::timestamp;
using stm::timestamp_max;
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
  struct OrecEagerRedo
  {
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
  };

  /**
   *  OrecEagerRedo begin:
   *
   *    Standard begin: just get a start time
   */
  bool
  OrecEagerRedo::begin()
  {
      Self.allocator.onTxBegin();
      Self.start_time = timestamp.val;
      return false;
  }

  /**
   *  OrecEagerRedo commit (read-only):
   *
   *    Standard commit: we hold no locks, and we're valid, so just clean up
   */
  void
  OrecEagerRedo::commit_ro()
  {
      Self.r_orecs.reset();
      OnReadOnlyCommit();
  }

  /**
   *  OrecEagerRedo commit (writing context):
   *
   *    Since we hold all locks, and since we use Wang-style timestamps, we
   *    need to validate, run the redo log, and then get a timestamp and
   *    release locks.
   */
  void
  OrecEagerRedo::commit_rw()
  {
      // note: we're using timestamps in the same manner as
      // OrecLazy... without the single-thread optimization

      // we have all locks, so validate
      foreach (OrecList, i, Self.r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > Self.start_time) && (ivt != Self.my_lock.all))
              Self.tmabort();
      }

      // run the redo log
      Self.writes.writeback();

      // we're a writer, so increment the global timestamp
      Self.end_time = 1 + faiptr(&timestamp.val);

      // release locks
      foreach (OrecList, i, Self.locks)
          (*i)->v.all = Self.end_time;

      // clean up
      Self.r_orecs.reset();
      Self.writes.reset();
      Self.locks.reset();
      OnReadWriteCommit( read_ro, write_ro, commit_ro);
  }

  /**
   *  OrecEagerRedo read (read-only transaction)
   *
   *    Since we don't hold locks in an RO transaction, this code is very
   *    simple: read the location, check the orec, and scale the timestamp if
   *    necessary.
   */
  void*
  OrecEagerRedo::read_ro(STM_READ_SIG(addr,))
  {
      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the location
          void* tmp = *addr;
          CFENCE;
          // read orec
          id_version_t ivt; ivt.all = o->v.all;

          // common case: new read to uncontended location
          if (ivt.all <= Self.start_time) {
              Self.r_orecs.insert(o);
              return tmp;
          }

          // abort if locked by other
          if (ivt.fields.lock)
              Self.tmabort();

          // scale timestamp if ivt is too new
          uintptr_t newts = timestamp.val;
          validate();
          Self.start_time = newts;
      }
  }

  /**
   *  OrecEagerRedo read (writing transaction)
   *
   *    The RW read code is slightly more complicated.  We only check the read
   *    log if we hold the lock, but we must be prepared for that possibility.
   */
  void*
  OrecEagerRedo::read_rw(STM_READ_SIG(addr,mask))
  {
      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the location
          void* tmp = *addr;
          CFENCE;
          // read orec
          id_version_t ivt; ivt.all = o->v.all;

          // common case: new read to uncontended location
          if (ivt.all <= Self.start_time) {
              Self.r_orecs.insert(o);
              return tmp;
          }

          // next best: locked by me
          if (ivt.all == Self.my_lock.all) {
              // check the log for a RAW hazard, we expect to miss
              WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
              bool found = Self.writes.find(log);
              REDO_RAW_CHECK(found, log, mask);
              REDO_RAW_CLEANUP(tmp, found, log, mask);
              return tmp;
          }

          // abort if locked by other
          if (ivt.fields.lock)
              Self.tmabort();

          // scale timestamp if ivt is too new
          uintptr_t newts = timestamp.val;
          validate();
          Self.start_time = newts;
      }
  }

  /**
   *  OrecEagerRedo write (read-only context)
   *
   *    To write, put the value in the write buffer, then try to lock the orec.
   *
   *    NB: saving the value first decreases register pressure
   */
  void
  OrecEagerRedo::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      // add to redo log
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec version number
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: uncontended location... lock it
          if (ivt.all <= Self.start_time) {
              if (!bcasptr(&o->v.all, ivt.all, Self.my_lock.all))
                  Self.tmabort();

              // save old, log lock, write, return
              o->p = ivt.all;
              Self.locks.insert(o);
              OnFirstWrite( read_rw, write_rw, commit_rw);
              return;
          }

          // fail if lock held
          if (ivt.fields.lock)
              Self.tmabort();

          // unlocked but too new... scale forward and try again
          uintptr_t newts = timestamp.val;
          validate();
          Self.start_time = newts;
      }
  }

  /**
   *  OrecEagerRedo write (writing context)
   *
   *    This is just like above, but with a condition for when the lock is held
   *    by the caller.
   */
  void
  OrecEagerRedo::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      // add to redo log
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec version number
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: uncontended location... lock it
          if (ivt.all <= Self.start_time) {
              if (!bcasptr(&o->v.all, ivt.all, Self.my_lock.all))
                  Self.tmabort();

              // save old, log lock, write, return
              o->p = ivt.all;
              Self.locks.insert(o);
              return;
          }

          // next best: already have the lock
          if (ivt.all == Self.my_lock.all)
              return;

          // fail if lock held
          if (ivt.fields.lock)
              Self.tmabort();

          // unlocked but too new... scale forward and try again
          uintptr_t newts = timestamp.val;
          validate();
          Self.start_time = newts;
      }
  }

  /**
   *  OrecEagerRedo unwinder:
   *
   *    To unwind, we must release locks, but we don't have an undo log to run.
   */
  stm::scope_t*
  OrecEagerRedo::rollback(STM_ROLLBACK_SIG( except, len))
  {
      PreRollback();

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(Self.writes, except, len);

      // release the locks and restore version numbers
      foreach (OrecList, i, Self.locks)
          (*i)->v.all = (*i)->p;

      // undo memory operations, reset lists
      Self.r_orecs.reset();
      Self.writes.reset();
      Self.locks.reset();
      return PostRollback( read_ro, write_ro, commit_ro);
  }

  /**
   *  OrecEagerRedo in-flight irrevocability: use abort-and-restart.
   */
  bool
  OrecEagerRedo::irrevoc()
  {
      return false;
  }

  /**
   *  OrecEagerRedo validation
   *
   *    validate the read set by making sure that all orecs that we've read have
   *    timestamps older than our start time, unless we locked those orecs.
   */
  void
  OrecEagerRedo::validate()
  {
      foreach (OrecList, i, Self.r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > Self.start_time) && (ivt != Self.my_lock.all))
              Self.tmabort();
      }
  }

  /**
   *  Switch to OrecEagerRedo:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   */
  void
  OrecEagerRedo::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
  }
}

namespace stm {
  /**
   *  OrecEagerRedo initialization
   */
  template<>
  void initTM<OrecEagerRedo>()
  {
      // set the name
      stms[OrecEagerRedo].name      = "OrecEagerRedo";

      // set the pointers
      stms[OrecEagerRedo].begin     = ::OrecEagerRedo::begin;
      stms[OrecEagerRedo].commit    = ::OrecEagerRedo::commit_ro;
      stms[OrecEagerRedo].read      = ::OrecEagerRedo::read_ro;
      stms[OrecEagerRedo].write     = ::OrecEagerRedo::write_ro;
      stms[OrecEagerRedo].rollback  = ::OrecEagerRedo::rollback;
      stms[OrecEagerRedo].irrevoc   = ::OrecEagerRedo::irrevoc;
      stms[OrecEagerRedo].switcher  = ::OrecEagerRedo::onSwitchTo;
      stms[OrecEagerRedo].privatization_safe = false;
  }
}
