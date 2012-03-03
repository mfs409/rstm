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
 *  TMLLazy Implementation
 *
 *    This is just like TML, except that we use buffered update and we wait to
 *    become the 'exclusive writer' until commit time.  The idea is that this
 *    is supposed to increase concurrency, and also that this should be quite
 *    fast even though it has the function call overhead.  This algorithm
 *    provides at least ALA semantics.
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

using stm::TxThread;
using stm::timestamp;
using stm::WriteSetEntry;
using stm::Self;
using stm::OnFirstWrite;
using stm::OnReadWriteCommit;
using stm::OnReadOnlyCommit;
using stm::PreRollback;
using stm::PostRollback;
/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.  Note that with TML, we don't expect the reads and
 *  writes to be called, because we expect the isntrumentation to be inlined
 *  via the dispatch mechanism.  However, we must provide the code to handle
 *  the uncommon case.
 */
namespace {
  struct TMLLazy {
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
   *  TMLLazy begin:
   */
  bool
  TMLLazy::begin()
  {
      // Sample the sequence lock until it is even (unheld)
      while ((Self.start_time = timestamp.val)&1)
          spin64();

      // notify the allocator
      Self.allocator.onTxBegin();
      return false;
  }

  /**
   *  TMLLazy commit (read-only context):
   */
  void
  TMLLazy::commit_ro()
  {
      // no metadata to manage, so just be done!
      OnReadOnlyCommit();
  }

  /**
   *  TMLLazy commit (writer context):
   */
  void
  TMLLazy::commit_rw()
  {
      // we have writes... if we can't get the lock, abort
      if (!bcasptr(&timestamp.val, Self.start_time, Self.start_time + 1))
          Self.tmabort();

      // we're committed... run the redo log
      Self.writes.writeback();

      // release the sequence lock and clean up
      timestamp.val++;
      Self.writes.reset();
      OnReadWriteCommit( read_ro, write_ro, commit_ro);
  }

  /**
   *  TMLLazy read (read-only context)
   */
  void*
  TMLLazy::read_ro(STM_READ_SIG(addr,))
  {
      // read the actual value, direct from memory
      void* tmp = *addr;
      CFENCE;

      // if the lock has changed, we must fail
      //
      // NB: this form of /if/ appears to be faster
      if (__builtin_expect(timestamp.val == Self.start_time, true))
          return tmp;
      Self.tmabort();
      // unreachable
      return NULL;
  }

  /**
   *  TMLLazy read (writing context)
   */
  void*
  TMLLazy::read_rw(STM_READ_SIG(addr,mask))
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
   *  TMLLazy write (read-only context):
   */
  void
  TMLLazy::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      // do a buffered write
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite( read_rw, write_rw, commit_rw);
  }

  /**
   *  TMLLazy write (writing context):
   */
  void
  TMLLazy::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      // do a buffered write
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  TMLLazy unwinder
   */
  stm::scope_t*
  TMLLazy::rollback(STM_ROLLBACK_SIG( except, len))
  {
      PreRollback();
      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(Self.writes, except, len);

      Self.writes.reset();
      return PostRollback( read_ro, write_ro, commit_ro);
  }

  /**
   *  TMLLazy in-flight irrevocability:
   */
  bool
  TMLLazy::irrevoc()
  {
      // we are running in isolation by the time this code is run.  Make sure
      // we are valid.
      if (!bcasptr(&timestamp.val, Self.start_time, Self.start_time + 1))
          return false;

      // push all writes back to memory and clear writeset
      Self.writes.writeback();
      timestamp.val++;

      // return the STM to a state where it can be used after we finish our
      // irrevoc transaction
      Self.writes.reset();
      return true;
  }

  /**
   *  Switch to TMLLazy:
   *
   *    We just need to be sure that the timestamp is not odd
   */
  void
  TMLLazy::onSwitchTo()
  {
      if (timestamp.val & 1)
          ++timestamp.val;
  }
}

namespace stm {
  /**
   *  TMLLazy initialization
   */
  template<>
  void initTM<TMLLazy>()
  {
      // set the name
      stm::stms[TMLLazy].name     = "TMLLazy";

      // set the pointers
      stm::stms[TMLLazy].begin    = ::TMLLazy::begin;
      stm::stms[TMLLazy].commit   = ::TMLLazy::commit_ro;
      stm::stms[TMLLazy].read     = ::TMLLazy::read_ro;
      stm::stms[TMLLazy].write    = ::TMLLazy::write_ro;
      stm::stms[TMLLazy].rollback = ::TMLLazy::rollback;
      stm::stms[TMLLazy].irrevoc  = ::TMLLazy::irrevoc;
      stm::stms[TMLLazy].switcher = ::TMLLazy::onSwitchTo;
      stm::stms[TMLLazy].privatization_safe = true;
  }
}
