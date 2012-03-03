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
 *  TLI Implementation
 *
 *    This is a variant of InvalSTM.  We use 1024-bit filters, and standard
 *    "first committer wins" contention management.  What makes this algorithm
 *    interesting is that we replace all the locking from InvalSTM with
 *    optimistic mechanisms.
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

using stm::UNRECOVERABLE;
using stm::TxThread;
using stm::timestamp;
using stm::threads;
using stm::threadcount;
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
  struct TLI
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
  };


  /**
   *  TLI begin:
   */
  bool
  TLI::begin()
  {
      // mark self as alive
      Self.allocator.onTxBegin();
      Self.alive = 1;
      return false;
  }

  /**
   *  TLI commit (read-only):
   */
  void
  TLI::commit_ro()
  {
      // if the transaction is invalid, abort
      if (__builtin_expect(Self.alive == 2, false))
          Self.tmabort();

      // ok, all is good
      Self.alive = 0;
      Self.rf->clear();
      OnReadOnlyCommit();
  }

  /**
   *  TLI commit (writing context):
   */
  void
  TLI::commit_rw()
  {
      // if the transaction is invalid, abort
      if (__builtin_expect(Self.alive == 2, false))
          Self.tmabort();

      // grab the lock to stop the world
      uintptr_t tmp = timestamp.val;
      while (((tmp&1) == 1) || (!bcasptr(&timestamp.val, tmp, (tmp+1)))) {
          tmp = timestamp.val;
          spin64();
      }

      // double check that we're valid
      if (__builtin_expect(Self.alive == 2,false)) {
          timestamp.val = tmp + 2; // release the lock
          Self.tmabort();
      }

      // kill conflicting transactions
      for (uint32_t i = 0; i < threadcount.val; i++)
          if ((threads[i]->alive == 1) && (Self.wf->intersect(threads[i]->rf)))
              threads[i]->alive = 2;

      // do writeback
      Self.writes.writeback();

      // release the lock and clean up
      Self.alive = 0;
      timestamp.val = tmp+2;
      Self.writes.reset();
      Self.rf->clear();
      Self.wf->clear();
      OnReadWriteCommit( read_ro, write_ro, commit_ro);
  }

  /**
   *  TLI read (read-only transaction)
   *
   *    We do a visible read, so we must write the fact of this read before we
   *    actually access memory.  Then, we must be sure to perform the read during
   *    a period when the world is not stopped for writeback.  Lastly, we must
   *    ensure that we are still valid
   */
  void*
  TLI::read_ro(STM_READ_SIG(addr,))
  {
      // push address into read filter, ensure ordering w.r.t. the subsequent
      // read of data
      Self.rf->atomic_add(addr);

      // get a consistent snapshot of the value
      while (true) {
          uintptr_t x1 = timestamp.val;
          CFENCE;
          void* val = *addr;
          CFENCE;
          // if the ts was even and unchanged, then the read is valid
          bool ts_ok = !(x1&1) && (timestamp.val == x1);
          CFENCE;
          // if read valid, and we're not killed, return the value
          if ((Self.alive == 1) && ts_ok)
              return val;
          // abort if we're killed
          if (Self.alive == 2)
              Self.tmabort();
      }
  }

  /**
   *  TLI read (writing transaction)
   */
  void*
  TLI::read_rw(STM_READ_SIG(addr,mask))
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
   *  TLI write (read-only context)
   */
  void
  TLI::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      // buffer the write, update the filter
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      Self.wf->add(addr);
      OnFirstWrite( read_rw, write_rw, commit_rw);
  }

  /**
   *  TLI write (writing context)
   *
   *    Just like the RO case
   */
  void
  TLI::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      Self.wf->add(addr);
  }

  /**
   *  TLI unwinder:
   */
  stm::scope_t*
  TLI::rollback(STM_ROLLBACK_SIG( except, len))
  {
      PreRollback();

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(Self.writes, except, len);

      // clear filters and logs
      Self.rf->clear();
      if (Self.writes.size()) {
          Self.writes.reset();
          Self.wf->clear();
      }
      return PostRollback( read_ro, write_ro, commit_ro);
  }

  /**
   *  TLI in-flight irrevocability: use abort-and-restart
   */
  bool TLI::irrevoc() { return false; }

  /**
   *  Switch to TLI:
   *
   *    Must be sure the timestamp is not odd.
   */
  void TLI::onSwitchTo()
  {
      if (timestamp.val & 1)
          ++timestamp.val;
  }
}

namespace stm {
  /**
   *  TLI initialization
   */
  template<>
  void initTM<TLI>()
  {
      // set the name
      stms[TLI].name      = "TLI";

      // set the pointers
      stms[TLI].begin     = ::TLI::begin;
      stms[TLI].commit    = ::TLI::commit_ro;
      stms[TLI].read      = ::TLI::read_ro;
      stms[TLI].write     = ::TLI::write_ro;
      stms[TLI].rollback  = ::TLI::rollback;
      stms[TLI].irrevoc   = ::TLI::irrevoc;
      stms[TLI].switcher  = ::TLI::onSwitchTo;
      stms[TLI].privatization_safe = true;
  }
}
