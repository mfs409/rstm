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
 *  BitEagerRedo Implementation
 *
 *    This is like BitEager, but instead of in-place update, we use redo logs.
 *    Note that we still have eager acquire.
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

using stm::UNRECOVERABLE;
using stm::TxThread;
using stm::BitLockList;
using stm::bitlock_t;
using stm::get_bitlock;
using stm::WriteSetEntry;
using stm::rrec_t;
using stm::Self;
using stm::OnReadOnlyCommit;
using stm::OnReadWriteCommit;
using stm::OnFirstWrite;
using stm::PreRollback;
using stm::PostRollback;
using stm::exp_backoff;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  struct BitEagerRedo
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
   *  These defines are for tuning backoff behavior
   */
#if defined(STM_CPU_SPARC)
#  define READ_TIMEOUT        32
#  define ACQUIRE_TIMEOUT     128
#  define DRAIN_TIMEOUT       1024
#else // STM_CPU_X86
#  define READ_TIMEOUT        32
#  define ACQUIRE_TIMEOUT     128
#  define DRAIN_TIMEOUT       256
#endif

  /**
   *  BitEagerRedo begin:
   */
  bool
  BitEagerRedo::begin()
  {
      Self.allocator.onTxBegin();
      return false;
  }

  /**
   *  BitEagerRedo commit (read-only):
   */
  void
  BitEagerRedo::commit_ro()
  {
      // read-only... release read locks
      foreach (BitLockList, i, Self.r_bitlocks)
          (*i)->readers.unsetbit(Self.id-1);

      Self.r_bitlocks.reset();
      OnReadOnlyCommit();
  }

  /**
   *  BitEagerRedo commit (writing context):
   */
  void
  BitEagerRedo::commit_rw()
  {
      // replay redo log
      Self.writes.writeback();
      CFENCE;

      // release write locks, then read locks
      foreach (BitLockList, i, Self.w_bitlocks)
          (*i)->owner = 0;
      foreach (BitLockList, i, Self.r_bitlocks)
          (*i)->readers.unsetbit(Self.id-1);

      // clean-up
      Self.r_bitlocks.reset();
      Self.w_bitlocks.reset();
      Self.writes.reset();
      OnReadWriteCommit(read_ro, write_ro, commit_ro);
  }

  /**
   *  BitEagerRedo read (read-only transaction)
   *
   *    As in BitEager, we use timeout for conflict resolution
   */
  void*
  BitEagerRedo::read_ro(STM_READ_SIG(addr,))
  {
      uint32_t tries = 0;
      bitlock_t* lock = get_bitlock(addr);

      // do I have a read lock?
      if (lock->readers.getbit(Self.id-1))
          return *addr;

      // log this location
      Self.r_bitlocks.insert(lock);

      // now try to get a read lock
      while (true) {
          // mark my reader bit
          lock->readers.setbit(Self.id-1);

          // if nobody has the write lock, we're done
          if (__builtin_expect(lock->owner == 0, true))
              return *addr;

          // drop read lock, wait (with timeout) for lock release
          lock->readers.unsetbit(Self.id-1);
          while (lock->owner != 0) {
              if (++tries > READ_TIMEOUT)
                  Self.tmabort();
          }
      }
  }

  /**
   *  BitEagerRedo read (writing transaction)
   *
   *    Same as RO case, but if we have the write lock, we can take a fast path
   */
  void*
  BitEagerRedo::read_rw(STM_READ_SIG(addr,mask))
  {
      uint32_t tries = 0;
      bitlock_t* lock = get_bitlock(addr);

      // do I have the write lock?
      if (lock->owner == Self.id) {
          // check the log
          WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
          bool found = Self.writes.find(log);
          REDO_RAW_CHECK(found, log, mask);

          void* val = *addr;
          REDO_RAW_CLEANUP(val, found, log, mask);
          return val;
      }

      // do I have a read lock?
      if (lock->readers.getbit(Self.id-1))
          return *addr;

      // log this location
      Self.r_bitlocks.insert(lock);

      // now try to get a read lock
      while (true) {
          // mark my reader bit
          lock->readers.setbit(Self.id-1);

          // if nobody has the write lock, we're done
          if (__builtin_expect(lock->owner == 0, true))
              return *addr;

          // drop read lock, wait (with timeout) for lock release
          lock->readers.unsetbit(Self.id-1);
          while (lock->owner != 0) {
              if (++tries > READ_TIMEOUT)
                  Self.tmabort();
          }
      }
  }

  /**
   *  BitEagerRedo write (read-only context)
   *
   *    Lock the location, then put the value in the write log
   */
  void
  BitEagerRedo::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      uint32_t tries = 0;
      bitlock_t* lock = get_bitlock(addr);

      // get the write lock, with timeout
      while (!bcasptr(&(lock->owner), 0u, Self.id))
          if (++tries > ACQUIRE_TIMEOUT)
              Self.tmabort();

      // log the lock, drop any read locks I have
      Self.w_bitlocks.insert(lock);
      lock->readers.unsetbit(Self.id-1);

      // wait (with timeout) for readers to drain out
      // (read one bucket at a time)
      for (unsigned b = 0; b < rrec_t::BUCKETS; ++b) {
          tries = 0;
          while (lock->readers.bits[b])
              if (++tries > DRAIN_TIMEOUT)
                  Self.tmabort();
      }

      // record in redo log
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      OnFirstWrite(read_rw, write_rw, commit_rw);
  }

  /**
   *  BitEagerRedo write (writing context)
   *
   *    Same as RO case, but with fastpath for repeat writes to same location
   */
  void
  BitEagerRedo::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      uint32_t tries = 0;
      bitlock_t* lock = get_bitlock(addr);

      // If I have the write lock, record in redo log, return
      if (lock->owner == Self.id) {
          Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
          return;
      }

      // get the write lock, with timeout
      while (!bcasptr(&(lock->owner), 0u, Self.id))
          if (++tries > ACQUIRE_TIMEOUT)
              Self.tmabort();

      // log the lock, drop any read locks I have
      Self.w_bitlocks.insert(lock);
      lock->readers.unsetbit(Self.id-1);

      // wait (with timeout) for readers to drain out
      // (read one bucket at a time)
      for (unsigned b = 0; b < rrec_t::BUCKETS; ++b) {
          tries = 0;
          while (lock->readers.bits[b])
              if (++tries > DRAIN_TIMEOUT)
                  Self.tmabort();
      }

      // record in redo log
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  BitEagerRedo unwinder:
   */
  stm::scope_t*
  BitEagerRedo::rollback(STM_ROLLBACK_SIG(except, len))
  {
      PreRollback();

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(Self.writes, except, len);

      // release write locks, then read locks
      foreach (BitLockList, i, Self.w_bitlocks)
          (*i)->owner = 0;
      foreach (BitLockList, i, Self.r_bitlocks)
          (*i)->readers.unsetbit(Self.id-1);

      // reset lists
      Self.r_bitlocks.reset();
      Self.w_bitlocks.reset();
      Self.writes.reset();

      // randomized exponential backoff
      exp_backoff();

      return PostRollback(read_ro, write_ro, commit_ro);
  }

  /**
   *  BitEagerRedo in-flight irrevocability:
   */
  bool BitEagerRedo::irrevoc()
  {
      return false;
  }

  /**
   *  Switch to BitEagerRedo:
   *
   *    The only global metadata used by BitEagerRedo is the bitlocks array,
   *    which should be all zeros.
   */
  void BitEagerRedo::onSwitchTo() {
  }
}

namespace stm {
  /**
   *  BitEagerRedo initialization
   */
  template<>
  void initTM<BitEagerRedo>()
  {
      // set the name
      stms[BitEagerRedo].name      = "BitEagerRedo";

      // set the pointers
      stms[BitEagerRedo].begin     = ::BitEagerRedo::begin;
      stms[BitEagerRedo].commit    = ::BitEagerRedo::commit_ro;
      stms[BitEagerRedo].read      = ::BitEagerRedo::read_ro;
      stms[BitEagerRedo].write     = ::BitEagerRedo::write_ro;
      stms[BitEagerRedo].rollback  = ::BitEagerRedo::rollback;
      stms[BitEagerRedo].irrevoc   = ::BitEagerRedo::irrevoc;
      stms[BitEagerRedo].switcher  = ::BitEagerRedo::onSwitchTo;
      stms[BitEagerRedo].privatization_safe = true;
  }
}
