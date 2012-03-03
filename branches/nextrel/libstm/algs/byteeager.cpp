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
 *  ByteEager Implementation
 *
 *    This is a good-faith implementation of the TLRW algorithm by Dice and
 *    Shavit, from SPAA 2010.  We use bytelocks, eager acquire, and in-place
 *    update, with timeout for deadlock avoidance.
 */

#include "../profiling.hpp"
#include "algs.hpp"

using stm::UNRECOVERABLE;
using stm::TxThread;
using stm::ByteLockList;
using stm::bytelock_t;
using stm::get_bytelock;
using stm::UndoLogEntry;
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
  struct ByteEager
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
   *  ByteEager begin:
   */
  bool
  ByteEager::begin()
  {
      Self.allocator.onTxBegin();
      return false;
  }

  /**
   *  ByteEager commit (read-only):
   */
  void
  ByteEager::commit_ro()
  {
      // read-only... release read locks
      foreach (ByteLockList, i, Self.r_bytelocks)
          (*i)->reader[Self.id-1] = 0;

      Self.r_bytelocks.reset();
      OnReadOnlyCommit();
  }

  /**
   *  ByteEager commit (writing context):
   */
  void
  ByteEager::commit_rw()
  {
      // release write locks, then read locks
      foreach (ByteLockList, i, Self.w_bytelocks)
          (*i)->owner = 0;
      foreach (ByteLockList, i, Self.r_bytelocks)
          (*i)->reader[Self.id-1] = 0;

      // clean-up
      Self.r_bytelocks.reset();
      Self.w_bytelocks.reset();
      Self.undo_log.reset();
      OnReadWriteCommit( read_ro, write_ro, commit_ro);
  }

  /**
   *  ByteEager read (read-only transaction)
   */
  void*
  ByteEager::read_ro(STM_READ_SIG(addr,))
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
   *  ByteEager read (writing transaction)
   */
  void*
  ByteEager::read_rw(STM_READ_SIG(addr,))
  {
      uint32_t tries = 0;
      bytelock_t* lock = get_bytelock(addr);

      // do I have the write lock?
      if (lock->owner == Self.id)
          return *addr;

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
          while (lock->owner != 0)
              if (++tries > READ_TIMEOUT)
                  Self.tmabort();
      }
  }

  /**
   *  ByteEager write (read-only context)
   */
  void
  ByteEager::write_ro(STM_WRITE_SIG(addr,val,mask))
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

      // add to undo log, do in-place write
      Self.undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
      STM_DO_MASKED_WRITE(addr, val, mask);

      OnFirstWrite( read_rw, write_rw, commit_rw);
  }

  /**
   *  ByteEager write (writing context)
   */
  void
  ByteEager::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      uint32_t tries = 0;
      bytelock_t* lock = get_bytelock(addr);

      // If I have the write lock, add to undo log, do write, return
      if (lock->owner == Self.id) {
          Self.undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
          STM_DO_MASKED_WRITE(addr, val, mask);
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

      // add to undo log, do in-place write
      Self.undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
      STM_DO_MASKED_WRITE(addr, val, mask);
  }

  /**
   *  ByteEager unwinder:
   */
  stm::scope_t*
  ByteEager::rollback(STM_ROLLBACK_SIG( except, len))
  {
      PreRollback();

      // Undo the writes, while at the same time watching out for the exception
      // object.
      STM_UNDO(Self.undo_log, except, len);

      // release write locks, then read locks
      foreach (ByteLockList, i, Self.w_bytelocks)
          (*i)->owner = 0;
      foreach (ByteLockList, i, Self.r_bytelocks)
          (*i)->reader[Self.id-1] = 0;

      // reset lists
      Self.r_bytelocks.reset();
      Self.w_bytelocks.reset();
      Self.undo_log.reset();

      // randomized exponential backoff
      exp_backoff();

      return PostRollback( read_ro, write_ro, commit_ro);
  }

  /**
   *  ByteEager in-flight irrevocability:
   */
  bool ByteEager::irrevoc()
  {
      return false;
  }

  /**
   *  Switch to ByteEager:
   */
  void ByteEager::onSwitchTo() {
  }
}

namespace stm {
  /**
   *  ByteEager initialization
   */
  template<>
  void initTM<ByteEager>()
  {
      // set the name
      stms[ByteEager].name      = "ByteEager";

      // set the pointers
      stms[ByteEager].begin     = ::ByteEager::begin;
      stms[ByteEager].commit    = ::ByteEager::commit_ro;
      stms[ByteEager].read      = ::ByteEager::read_ro;
      stms[ByteEager].write     = ::ByteEager::write_ro;
      stms[ByteEager].rollback  = ::ByteEager::rollback;
      stms[ByteEager].irrevoc   = ::ByteEager::irrevoc;
      stms[ByteEager].switcher  = ::ByteEager::onSwitchTo;
      stms[ByteEager].privatization_safe = true;
  }
}
