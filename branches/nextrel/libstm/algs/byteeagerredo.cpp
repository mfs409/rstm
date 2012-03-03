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
 *  ByteEagerRedo Implementation
 *
 *    This is like ByteEager, except we use redo logs instead of undo logs.  We
 *    still use eager locking.
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

using stm::UNRECOVERABLE;
using stm::TxThread;
using stm::ByteLockList;
using stm::bytelock_t;
using stm::get_bytelock;
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
namespace {
  struct ByteEagerRedo
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
   *  ByteEagerRedo begin:
   */
  bool
  ByteEagerRedo::begin()
  {
      Self.allocator.onTxBegin();
      return false;
  }

  /**
   *  ByteEagerRedo commit (read-only):
   */
  void
  ByteEagerRedo::commit_ro()
  {
      // read-only... release read locks
      foreach (ByteLockList, i, Self.r_bytelocks)
          (*i)->reader[Self.id-1] = 0;

      Self.r_bytelocks.reset();
      OnReadOnlyCommit();
  }

  /**
   *  ByteEagerRedo commit (writing context):
   */
  void
  ByteEagerRedo::commit_rw()
  {
      // replay redo log
      Self.writes.writeback();
      CFENCE;

      // release write locks, then read locks
      foreach (ByteLockList, i, Self.w_bytelocks)
          (*i)->owner = 0;
      foreach (ByteLockList, i, Self.r_bytelocks)
          (*i)->reader[Self.id-1] = 0;

      // clean-up
      Self.r_bytelocks.reset();
      Self.w_bytelocks.reset();
      Self.writes.reset();
      OnReadWriteCommit( read_ro, write_ro, commit_ro);
  }

  /**
   *  ByteEagerRedo read (read-only transaction)
   */
  void*
  ByteEagerRedo::read_ro(STM_READ_SIG(addr,))
  {
      uint32_t tries = 0;
      bytelock_t* lock = get_bytelock(addr);

      // do I have a read lock?
      if (lock->reader[Self.id-1] == 1)
          return *addr;

      // log this location
      Self.r_bytelocks.insert(lock);

      // now try to get a read lock
      while (true) {
          // mark my reader byte
          lock->set_read_byte(Self.id-1);
          // if nobody has the write lock, we're done
          if (__builtin_expect(lock->owner == 0, true))
              return *addr;

          // drop read lock, wait (with timeout) for lock release
          lock->reader[Self.id-1] = 0;
          while (lock->owner != 0) {
              if (++tries > READ_TIMEOUT)
                  Self.tmabort();
          }
      }
  }

  /**
   *  ByteEagerRedo read (writing transaction)
   */
  void*
  ByteEagerRedo::read_rw(STM_READ_SIG(addr,mask))
  {
      uint32_t tries = 0;
      bytelock_t* lock = get_bytelock(addr);

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
      if (lock->reader[Self.id-1] == 1)
          return *addr;

      // log this location
      Self.r_bytelocks.insert(lock);

      // now try to get a read lock
      while (true) {
          // mark my reader byte
          lock->set_read_byte(Self.id-1);
          // if nobody has the write lock, we're done
          if (__builtin_expect(lock->owner == 0, true))
              return *addr;

          // drop read lock, wait (with timeout) for lock release
          lock->reader[Self.id-1] = 0;
          while (lock->owner != 0) {
              if (++tries > READ_TIMEOUT)
                  Self.tmabort();
          }
      }
  }

  /**
   *  ByteEagerRedo write (read-only context)
   */
  void
  ByteEagerRedo::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      uint32_t tries = 0;
      bytelock_t* lock = get_bytelock(addr);

      // get the write lock, with timeout
      while (!bcas32(&(lock->owner), 0u, Self.id))
          if (++tries > ACQUIRE_TIMEOUT)
              Self.tmabort();

      // log the lock, drop any read locks I have
      Self.w_bytelocks.insert(lock);
      lock->reader[Self.id-1] = 0;

      // wait (with timeout) for readers to drain out
      // (read 4 bytelocks at a time)
      volatile uint32_t* lock_alias = (volatile uint32_t*)&lock->reader[0];
      for (int i = 0; i < 15; ++i) {
          tries = 0;
          while (lock_alias[i] != 0)
              if (++tries > DRAIN_TIMEOUT)
                  Self.tmabort();
      }

      // record in redo log
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      OnFirstWrite( read_rw, write_rw, commit_rw);
  }

  /**
   *  ByteEagerRedo write (writing context)
   */
  void
  ByteEagerRedo::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      uint32_t tries = 0;
      bytelock_t* lock = get_bytelock(addr);

      // If I have the write lock, record in redo log, return
      if (lock->owner == Self.id) {
          Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
          return;
      }

      // get the write lock, with timeout
      while (!bcas32(&(lock->owner), 0u, Self.id))
          if (++tries > ACQUIRE_TIMEOUT)
              Self.tmabort();

      // log the lock, drop any read locks I have
      Self.w_bytelocks.insert(lock);
      lock->reader[Self.id-1] = 0;

      // wait (with timeout) for readers to drain out
      // (read 4 bytelocks at a time)
      volatile uint32_t* lock_alias = (volatile uint32_t*)&lock->reader[0];
      for (int i = 0; i < 15; ++i) {
          tries = 0;
          while (lock_alias[i] != 0)
              if (++tries > DRAIN_TIMEOUT)
                  Self.tmabort();
      }

      // record in redo log
      Self.writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  ByteEagerRedo unwinder:
   */
  stm::scope_t*
  ByteEagerRedo::rollback(STM_ROLLBACK_SIG( except, len))
  {
      PreRollback();

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(Self.writes, except, len);

      // release write locks, then read locks
      foreach (ByteLockList, i, Self.w_bytelocks)
          (*i)->owner = 0;
      foreach (ByteLockList, i, Self.r_bytelocks)
          (*i)->reader[Self.id-1] = 0;

      // reset lists
      Self.r_bytelocks.reset();
      Self.w_bytelocks.reset();
      Self.writes.reset();

      // randomized exponential backoff
      exp_backoff();

      return PostRollback( read_ro, write_ro, commit_ro);
  }

  /**
   *  ByteEagerRedo in-flight irrevocability:
   */
  bool
  ByteEagerRedo::irrevoc()
  {
      return false;
  }

  /**
   *  Switch to ByteEagerRedo:
   */
  void
  ByteEagerRedo::onSwitchTo() {
  }
}

namespace stm {
  /**
   *  ByteEagerRedo initialization
   */
  template<>
  void initTM<ByteEagerRedo>()
  {
      // set the name
      stms[ByteEagerRedo].name      = "ByteEagerRedo";

      // set the pointers
      stms[ByteEagerRedo].begin     = ::ByteEagerRedo::begin;
      stms[ByteEagerRedo].commit    = ::ByteEagerRedo::commit_ro;
      stms[ByteEagerRedo].read      = ::ByteEagerRedo::read_ro;
      stms[ByteEagerRedo].write     = ::ByteEagerRedo::write_ro;
      stms[ByteEagerRedo].rollback  = ::ByteEagerRedo::rollback;
      stms[ByteEagerRedo].irrevoc   = ::ByteEagerRedo::irrevoc;
      stms[ByteEagerRedo].switcher  = ::ByteEagerRedo::onSwitchTo;
      stms[ByteEagerRedo].privatization_safe = true;
  }
}
