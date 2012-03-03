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
 *  RingSW Implementation
 *
 *    This is the "single writer" variant of the RingSTM algorithm, published
 *    by Spear et al. at SPAA 2008.  There are many optimizations, based on the
 *    Fastpath paper by Spear et al. LCPC 2009.
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

using stm::TxThread;
using stm::timestamp;
using stm::timestamp_max;
using stm::last_complete;
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
  struct RingSW {
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
      static NOINLINE void check_inflight(uintptr_t my_index);
  };

  /**
   *  RingSW begin:
   *
   *    To start a RingSW transaction, we need to find a ring entry that is
   *    writeback-complete.  In the old RingSW, this was hard.  In the new
   *    RingSW, inspired by FastPath, this is easy.
   */
  bool
  RingSW::begin()
  {
      Self.allocator.onTxBegin();
      // start time is when the last txn completed
      Self.start_time = last_complete.val;
      return false;
  }

  /**
   *  RingSW commit (read-only):
   */
  void
  RingSW::commit_ro()
  {
      // clear the filter and we are done
      Self.rf->clear();
      OnReadOnlyCommit();
  }

  /**
   *  RingSW commit (writing context):
   *
   *    This is the crux of the RingSTM algorithm, and also the foundation for
   *    other livelock-free STMs.  The main idea is that we use a single CAS to
   *    transition a valid transaction from a state in which it is invisible to a
   *    state in which it is logically committed.  This transition stops the
   *    world, while the logically committed transaction replays its writes.
   */
  void
  RingSW::commit_rw()
  {
      // get a commit time, but only succeed in the CAS if this transaction
      // is still valid
      uintptr_t commit_time;
      do {
          commit_time = timestamp.val;
          // get the latest ring entry, return if we've seen it already
          if (commit_time != Self.start_time) {
              // wait for the latest entry to be initialized
              while (last_init.val < commit_time)
                  spin64();

              // intersect against all new entries
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

      // copy the bits over (use SSE, not indirection)
      ring_wf[(commit_time + 1) % RING_ELEMENTS].fastcopy(Self.wf);

      // setting this says "the bits are valid"
      last_init.val = commit_time + 1;

      // we're committed... run redo log, then mark ring entry COMPLETE
      Self.writes.writeback();
      last_complete.val = commit_time + 1;

      // clean up
      Self.writes.reset();
      Self.rf->clear();
      Self.wf->clear();
      OnReadWriteCommit( read_ro, write_ro, commit_ro);
  }

  /**
   *  RingSW read (read-only transaction)
   */
  void*
  RingSW::read_ro(STM_READ_SIG(addr,))
  {
      // read the value from memory, log the address, and validate
      void* val = *addr;
      CFENCE;
      Self.rf->add(addr);
      // get the latest initialized ring entry, return if we've seen it already
      uintptr_t my_index = last_init.val;
      if (__builtin_expect(my_index != Self.start_time, false))
          check_inflight( my_index);
      return val;
  }

  /**
   *  RingSW read (writing transaction)
   */
  void*
  RingSW::read_rw(STM_READ_SIG(addr,mask))
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
   *  RingSW write (read-only context)
   */
  void
  RingSW::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      // buffer the write and update the filter
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      Self.wf->add(addr);
      OnFirstWrite( read_rw, write_rw, commit_rw);
  }

  /**
   *  RingSW write (writing context)
   */
  void
  RingSW::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      Self.wf->add(addr);
  }

  /**
   *  RingSW unwinder:
   */
  stm::scope_t*
  RingSW::rollback(STM_ROLLBACK_SIG( except, len))
  {
      PreRollback();

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(Self.writes, except, len);

      // reset filters and lists
      Self.rf->clear();
      if (Self.writes.size()) {
          Self.writes.reset();
          Self.wf->clear();
      }
      return PostRollback( read_ro, write_ro, commit_ro);
  }

  /**
   *  RingSW in-flight irrevocability: use abort-and-restart
   */
  bool
  RingSW::irrevoc()
  {
      return false;
  }

  /**
   *  RingSW validation
   *
   *    check the ring for new entries and validate against them
   */
  void
  RingSW::check_inflight(uintptr_t my_index)
  {
      // intersect against all new entries
      for (uintptr_t i = my_index; i >= Self.start_time + 1; i--)
          if (ring_wf[i % RING_ELEMENTS].intersect(Self.rf))
              Self.tmabort();

      // wait for newest entry to be writeback-complete before returning
      while (last_complete.val < my_index)
          spin64();

      // detect ring rollover: start.ts must not have changed
      if (timestamp.val > (Self.start_time + RING_ELEMENTS))
          Self.tmabort();

      // ensure this tx doesn't look at this entry again
      Self.start_time = my_index;
  }

  /**
   *  Switch to RingSW:
   *
   *    It really doesn't matter *where* in the ring we start.  What matters is
   *    that the timestamp, last_init, and last_complete are equal.
   */
  void
  RingSW::onSwitchTo()
  {
      last_init.val = timestamp.val;
      last_complete.val = last_init.val;
  }
}

namespace stm {
  /**
   *  RingSW initialization
   */
  template<>
  void initTM<RingSW>()
  {
      // set the name
      stm::stms[RingSW].name      = "RingSW";

      // set the pointers
      stm::stms[RingSW].begin     = ::RingSW::begin;
      stm::stms[RingSW].commit    = ::RingSW::commit_ro;
      stm::stms[RingSW].read      = ::RingSW::read_ro;
      stm::stms[RingSW].write     = ::RingSW::write_ro;
      stm::stms[RingSW].rollback  = ::RingSW::rollback;
      stm::stms[RingSW].irrevoc   = ::RingSW::irrevoc;
      stm::stms[RingSW].switcher  = ::RingSW::onSwitchTo;
      stm::stms[RingSW].privatization_safe = true;
  }
}
