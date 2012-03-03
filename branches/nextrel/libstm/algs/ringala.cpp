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
 *  RingALA Implementation
 *
 *    This is RingSW, extended to support ALA semantics.  We keep a
 *    thread-local filter that unions all write sets that have been posted
 *    since this transaction started, and use that filter to detect ALA
 *    conflicts on every read.
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

using stm::UNRECOVERABLE;
using stm::TxThread;
using stm::last_complete;
using stm::timestamp;
using stm::last_init;
using stm::ring_wf;
using stm::RING_ELEMENTS;
using stm::WriteSetEntry;
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
  struct RingALA
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
      static NOINLINE void update_cf();
  };

  /**
   *  RingALA begin:
   */
  bool
  RingALA::begin()
  {
      Self.allocator.onTxBegin();
      Self.start_time = last_complete.val;
      return false;
  }

  /**
   *  RingALA commit (read-only):
   */
  void
  RingALA::commit_ro()
  {
      // just clear the filters
      Self.rf->clear();
      Self.cf->clear();
      OnReadOnlyCommit();
  }

  /**
   *  RingALA commit (writing context):
   *
   *    The writer commit algorithm is the same as RingSW
   */
  void
  RingALA::commit_rw()
  {
      // get a commit time, but only succeed in the CAS if this transaction
      // is still valid
      uintptr_t commit_time;
      do {
          commit_time = timestamp.val;
          // get the latest ring entry, return if we've seen it already
          if (commit_time != Self.start_time) {
              // wait for the latest entry to be initialized
              //
              // NB: in RingSW, we wait for this entry to be complete...
              //     here we skip it, which will require us to repeat the
              //     loop... This decision should be revisited at some point
              if (last_init.val < commit_time)
                  commit_time--;
              // NB: we don't need to union these entries into CF and then
              // intersect CF with RF.  Instead, we can just intersect with
              // RF directly.  This is safe, because RF is guaranteed not to
              // change from here on out.
              for (uintptr_t i = commit_time; i >= Self.start_time + 1; i--)
                  if (ring_wf[i % RING_ELEMENTS].intersect(Self.rf))
                      Self.tmabort();

              // wait for newest entry to be wb-complete before continuing
              while (last_complete.val < commit_time)
                  spin64();

              // detect ring rollover: start.ts must not have changed
              if (timestamp.val > (Self.start_time + RING_ELEMENTS))
                  Self.tmabort();

              // ensure this tx doesn't look at this entry again
              Self.start_time = commit_time;
          }
      } while (!bcasptr(&timestamp.val, commit_time, commit_time + 1));

      // copy the bits over (use SSE)
      ring_wf[(commit_time + 1) % RING_ELEMENTS].fastcopy(Self.wf);

      // setting this says "the bits are valid"
      last_init.val = commit_time + 1;

      // we're committed... run redo log, then mark ring entry COMPLETE
      Self.writes.writeback();
      last_complete.val = commit_time + 1;

      // clean up
      Self.writes.reset();
      Self.rf->clear();
      Self.cf->clear();
      Self.wf->clear();
      OnReadWriteCommit( read_ro, write_ro, commit_ro);
  }

  /**
   *  RingALA read (read-only transaction)
   *
   *    RingALA reads are like RingSTM reads, except that we must also verify
   *    that our reads won't result in ALA conflicts
   */
  void*
  RingALA::read_ro(STM_READ_SIG(addr,))
  {
      // abort if this read would violate ALA
      if (Self.cf->lookup(addr))
          Self.tmabort();

      // read the value from memory, log the address, and validate
      void* val = *addr;
      CFENCE;
      Self.rf->add(addr);
      // get the latest initialized ring entry, return if we've seen it already
      if (__builtin_expect(last_init.val != Self.start_time, false))
          update_cf();
      return val;
  }

  /**
   *  RingALA read (writing transaction)
   */
  void*
  RingALA::read_rw(STM_READ_SIG(addr,mask))
  {
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = Self.writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // abort if this read would violate ALA
      if (Self.cf->lookup(addr))
          Self.tmabort();

      // read the value from memory, log the address, and validate
      void* val = *addr;
      CFENCE;
      Self.rf->add(addr);
      // get the latest initialized ring entry, return if we've seen it already
      if (__builtin_expect(last_init.val != Self.start_time, false))
          update_cf();

      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  RingALA write (read-only context)
   */
  void
  RingALA::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      // buffer the write and update the filter
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      Self.wf->add(addr);
      OnFirstWrite( read_rw, write_rw, commit_rw);
  }

  /**
   *  RingALA write (writing context)
   */
  void
  RingALA::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      Self.wf->add(addr);
  }

  /**
   *  RingALA unwinder:
   */
  stm::scope_t*
  RingALA::rollback(STM_ROLLBACK_SIG( except, len))
  {
      PreRollback();

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(Self.writes, except, len);

      // reset lists and filters
      Self.rf->clear();
      Self.cf->clear();
      if (Self.writes.size()) {
          Self.writes.reset();
          Self.wf->clear();
      }
      return PostRollback( read_ro, write_ro, commit_ro);
  }

  /**
   *  RingALA in-flight irrevocability:
   *
   *  NB: RingALA actually **must** use abort-and-restart to preserve ALA.
   */
  bool RingALA::irrevoc() { return false; }

  /**
   *  RingALA validation
   *
   *    For every new filter, add it to the conflict filter (cf).  Then intersect
   *    the read filter with the conflict filter to identify ALA violations.
   */
  void
  RingALA::update_cf()
  {
      // get latest entry
      uintptr_t my_index = last_init.val;

      // add all new entries to cf
      for (uintptr_t i = my_index; i >= Self.start_time + 1; i--)
          Self.cf->unionwith(ring_wf[i % RING_ELEMENTS]);

      CFENCE;
      // detect ring rollover: start.ts must not have changed
      if (timestamp.val > (Self.start_time + RING_ELEMENTS))
          Self.tmabort();

      // now intersect my rf with my cf
      if (Self.rf->intersect(Self.cf))
          Self.tmabort();

      // wait for newest entry to be writeback-complete before returning
      while (last_complete.val < my_index)
          spin64();

      // ensure this tx doesn't look at this entry again
      Self.start_time = my_index;
  }

  /**
   *  Switch to RingALA:
   *
   *    It really doesn't matter *where* in the ring we start.  What matters is
   *    that the timestamp, last_init, and last_complete are equal.
   */
  void
  RingALA::onSwitchTo()
  {
      last_init.val = timestamp.val;
      last_complete.val = last_init.val;
  }
}

namespace stm {
  /**
   *  RingALA initialization
   */
  template<>
  void initTM<RingALA>()
  {
      // set the name
      stms[RingALA].name      = "RingALA";

      // set the pointers
      stms[RingALA].begin     = ::RingALA::begin;
      stms[RingALA].commit    = ::RingALA::commit_ro;
      stms[RingALA].read      = ::RingALA::read_ro;
      stms[RingALA].write     = ::RingALA::write_ro;
      stms[RingALA].rollback  = ::RingALA::rollback;
      stms[RingALA].irrevoc   = ::RingALA::irrevoc;
      stms[RingALA].switcher  = ::RingALA::onSwitchTo;
      stms[RingALA].privatization_safe = true;
  }
}
