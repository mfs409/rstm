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
#include "../RedoRAWUtils.hpp"
#include "../cm.hpp"

using stm::TxThread;
using stm::ByteLockList;
using stm::bytelock_t;
using stm::get_bytelock;
using stm::WriteSetEntry;


/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  struct ByteEagerRedo
  {
      static void begin(TX_LONE_PARAMETER);
      static TM_FASTCALL void* read_ro(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro(TX_LONE_PARAMETER);
      static TM_FASTCALL void commit_rw(TX_LONE_PARAMETER);

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
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
  void ByteEagerRedo::begin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
  }

  /**
   *  ByteEagerRedo commit (read-only):
   */
  void
  ByteEagerRedo::commit_ro(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // read-only... release read locks
      foreach (ByteLockList, i, tx->r_bytelocks)
          (*i)->reader[tx->id-1] = 0;

      tx->r_bytelocks.reset();
      OnROCommit(tx);
  }

  /**
   *  ByteEagerRedo commit (writing context):
   */
  void
  ByteEagerRedo::commit_rw(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // replay redo log
      tx->writes.writeback();
      CFENCE;

      // release write locks, then read locks
      foreach (ByteLockList, i, tx->w_bytelocks)
          (*i)->owner = 0;
      foreach (ByteLockList, i, tx->r_bytelocks)
          (*i)->reader[tx->id-1] = 0;

      // clean-up
      tx->r_bytelocks.reset();
      tx->w_bytelocks.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  ByteEagerRedo read (read-only transaction)
   */
  void*
  ByteEagerRedo::read_ro(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      uint32_t tries = 0;
      bytelock_t* lock = get_bytelock(addr);

      // do I have a read lock?
      if (lock->reader[tx->id-1] == 1)
          return *addr;

      // log this location
      tx->r_bytelocks.insert(lock);

      // now try to get a read lock
      while (true) {
          // mark my reader byte
          lock->set_read_byte(tx->id-1);
          // if nobody has the write lock, we're done
          if (__builtin_expect(lock->owner == 0, true))
              return *addr;

          // drop read lock, wait (with timeout) for lock release
          lock->reader[tx->id-1] = 0;
          while (lock->owner != 0) {
              if (++tries > READ_TIMEOUT)
                  stm::tmabort();
          }
      }
  }

  /**
   *  ByteEagerRedo read (writing transaction)
   */
  void*
  ByteEagerRedo::read_rw(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      uint32_t tries = 0;
      bytelock_t* lock = get_bytelock(addr);

      // do I have the write lock?
      if (lock->owner == tx->id) {
          // check the log
          WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
          bool found = tx->writes.find(log);
          REDO_RAW_CHECK(found, log, mask);

          void* val = *addr;
          REDO_RAW_CLEANUP(val, found, log, mask);
          return val;
      }

      // do I have a read lock?
      if (lock->reader[tx->id-1] == 1)
          return *addr;

      // log this location
      tx->r_bytelocks.insert(lock);

      // now try to get a read lock
      while (true) {
          // mark my reader byte
          lock->set_read_byte(tx->id-1);
          // if nobody has the write lock, we're done
          if (__builtin_expect(lock->owner == 0, true))
              return *addr;

          // drop read lock, wait (with timeout) for lock release
          lock->reader[tx->id-1] = 0;
          while (lock->owner != 0) {
              if (++tries > READ_TIMEOUT)
                  stm::tmabort();
          }
      }
  }

  /**
   *  ByteEagerRedo write (read-only context)
   */
  void
  ByteEagerRedo::write_ro(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      uint32_t tries = 0;
      bytelock_t* lock = get_bytelock(addr);

      // get the write lock, with timeout
      while (!bcas32(&(lock->owner), 0u, tx->id))
          if (++tries > ACQUIRE_TIMEOUT)
              stm::tmabort();

      // log the lock, drop any read locks I have
      tx->w_bytelocks.insert(lock);
      lock->reader[tx->id-1] = 0;

      // wait (with timeout) for readers to drain out
      // (read 4 bytelocks at a time)
      volatile uint32_t* lock_alias = (volatile uint32_t*)&lock->reader[0];
      for (int i = 0; i < 15; ++i) {
          tries = 0;
          while (lock_alias[i] != 0)
              if (++tries > DRAIN_TIMEOUT)
                  stm::tmabort();
      }

      // record in redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      stm::OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  ByteEagerRedo write (writing context)
   */
  void
  ByteEagerRedo::write_rw(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      uint32_t tries = 0;
      bytelock_t* lock = get_bytelock(addr);

      // If I have the write lock, record in redo log, return
      if (lock->owner == tx->id) {
          tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
          return;
      }

      // get the write lock, with timeout
      while (!bcas32(&(lock->owner), 0u, tx->id))
          if (++tries > ACQUIRE_TIMEOUT)
              stm::tmabort();

      // log the lock, drop any read locks I have
      tx->w_bytelocks.insert(lock);
      lock->reader[tx->id-1] = 0;

      // wait (with timeout) for readers to drain out
      // (read 4 bytelocks at a time)
      volatile uint32_t* lock_alias = (volatile uint32_t*)&lock->reader[0];
      for (int i = 0; i < 15; ++i) {
          tries = 0;
          while (lock_alias[i] != 0)
              if (++tries > DRAIN_TIMEOUT)
                  stm::tmabort();
      }

      // record in redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  ByteEagerRedo unwinder:
   */
  void
  ByteEagerRedo::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // release write locks, then read locks
      foreach (ByteLockList, i, tx->w_bytelocks)
          (*i)->owner = 0;
      foreach (ByteLockList, i, tx->r_bytelocks)
          (*i)->reader[tx->id-1] = 0;

      // reset lists
      tx->r_bytelocks.reset();
      tx->w_bytelocks.reset();
      tx->writes.reset();

      // randomized exponential backoff
      exp_backoff(tx);

      PostRollback(tx);
      ResetToRO(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  ByteEagerRedo in-flight irrevocability:
   */
  bool
  ByteEagerRedo::irrevoc(TxThread*)
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

#ifdef STM_ONESHOT_ALG_ByteEagerRedo
DECLARE_AS_ONESHOT_NORMAL(ByteEagerRedo)
#endif
