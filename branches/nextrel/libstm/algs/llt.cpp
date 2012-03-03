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
 *  LLT Implementation
 *
 *    This STM very closely resembles the GV1 variant of TL2.  That is, it uses
 *    orecs and lazy acquire.  Its clock requires everyone to increment it to
 *    commit writes, but this allows for read-set validation to be skipped at
 *    commit time.  Most importantly, there is no in-flight validation: if a
 *    timestamp is greater than when the transaction sampled the clock at begin
 *    time, the transaction aborts.
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

using stm::TxThread;
using stm::timestamp;
using stm::timestamp_max;
using stm::WriteSet;
using stm::OrecList;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::orec_t;
using stm::get_orec;
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
  struct LLT
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
   *  LLT begin:
   */
  bool
  LLT::begin()
  {
      Self.allocator.onTxBegin();
      // get a start time
      Self.start_time = timestamp.val;
      return false;
  }

  /**
   *  LLT commit (read-only):
   */
  void
  LLT::commit_ro()
  {
      // read-only, so just reset lists
      Self.r_orecs.reset();
      OnReadOnlyCommit();
  }

  /**
   *  LLT commit (writing context):
   *
   *    Get all locks, validate, do writeback.  Use the counter to avoid some
   *    validations.
   */
  void
  LLT::commit_rw()
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

      // increment the global timestamp since we have writes
      uintptr_t end_time = 1 + faiptr(&timestamp.val);

      // skip validation if nobody else committed
      if (end_time != (Self.start_time + 1))
          validate();

      // run the redo log
      Self.writes.writeback();

      // release locks
      CFENCE;
      foreach (OrecList, i, Self.locks)
          (*i)->v.all = end_time;

      // clean-up
      Self.r_orecs.reset();
      Self.writes.reset();
      Self.locks.reset();
      OnReadWriteCommit( read_ro, write_ro, commit_ro);
  }

  /**
   *  LLT read (read-only transaction)
   *
   *    We use "check twice" timestamps in LLT
   */
  void*
  LLT::read_ro(STM_READ_SIG(addr,))
  {
      // get the orec addr
      orec_t* o = get_orec(addr);

      // read orec, then val, then orec
      uintptr_t ivt = o->v.all;
      CFENCE;
      void* tmp = *addr;
      CFENCE;
      uintptr_t ivt2 = o->v.all;
      // if orec never changed, and isn't too new, the read is valid
      if ((ivt <= Self.start_time) && (ivt == ivt2)) {
          // log orec, return the value
          Self.r_orecs.insert(o);
          return tmp;
      }
      // unreachable
      Self.tmabort();
      return NULL;
  }

  /**
   *  LLT read (writing transaction)
   */
  void*
  LLT::read_rw(STM_READ_SIG(addr,mask))
  {
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = Self.writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // get the orec addr
      orec_t* o = get_orec(addr);

      // read orec, then val, then orec
      uintptr_t ivt = o->v.all;
      CFENCE;
      void* tmp = *addr;
      CFENCE;
      uintptr_t ivt2 = o->v.all;

      // fixup is here to minimize the postvalidation orec read latency
      REDO_RAW_CLEANUP(tmp, found, log, mask);
      // if orec never changed, and isn't too new, the read is valid
      if ((ivt <= Self.start_time) && (ivt == ivt2)) {
          // log orec, return the value
          Self.r_orecs.insert(o);
          return tmp;
      }
      Self.tmabort();
      // unreachable
      return NULL;
  }

  /**
   *  LLT write (read-only context)
   */
  void
  LLT::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      // add to redo log
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite( read_rw, write_rw, commit_rw);
  }

  /**
   *  LLT write (writing context)
   */
  void
  LLT::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      // add to redo log
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  LLT unwinder:
   */
  stm::scope_t*
  LLT::rollback(STM_ROLLBACK_SIG( except, len))
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
   *  LLT in-flight irrevocability:
   */
  bool
  LLT::irrevoc()
  {
      return false;
  }

  /**
   *  LLT validation
   */
  void
  LLT::validate()
  {
      // validate
      foreach (OrecList, i, Self.r_orecs) {
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > Self.start_time) && (ivt != Self.my_lock.all))
              Self.tmabort();
      }
  }

  /**
   *  Switch to LLT:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   */
  void
  LLT::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
  }
}

namespace stm {
  /**
   *  LLT initialization
   */
  template<>
  void initTM<LLT>()
  {
      // set the name
      stms[LLT].name      = "LLT";

      // set the pointers
      stms[LLT].begin     = ::LLT::begin;
      stms[LLT].commit    = ::LLT::commit_ro;
      stms[LLT].read      = ::LLT::read_ro;
      stms[LLT].write     = ::LLT::write_ro;
      stms[LLT].rollback  = ::LLT::rollback;
      stms[LLT].irrevoc   = ::LLT::irrevoc;
      stms[LLT].switcher  = ::LLT::onSwitchTo;
      stms[LLT].privatization_safe = false;
  }
}
