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

#include "algs.hpp"
#include "../cm.hpp"

namespace stm
{
  TM_FASTCALL void* ByteEagerRedoReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* ByteEagerRedoReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void ByteEagerRedoWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void ByteEagerRedoWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void ByteEagerRedoCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void ByteEagerRedoCommitRW(TX_LONE_PARAMETER);

  /**
   *  ByteEagerRedo begin:
   */
  void ByteEagerRedoBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
  }

  /**
   *  ByteEagerRedo commit (read-only):
   */
  void ByteEagerRedoCommitRO(TX_LONE_PARAMETER)
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
  void ByteEagerRedoCommitRW(TX_LONE_PARAMETER)
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
      ResetToRO(tx, ByteEagerRedoReadRO, ByteEagerRedoWriteRO,
                ByteEagerRedoCommitRO);
  }

  /**
   *  ByteEagerRedo read (read-only transaction)
   */
  void* ByteEagerRedoReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
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
              if (++tries > BYTELOCK_READ_TIMEOUT)
                  tmabort();
          }
      }
  }

  /**
   *  ByteEagerRedo read (writing transaction)
   */
  void* ByteEagerRedoReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
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
              if (++tries > BYTELOCK_READ_TIMEOUT)
                  tmabort();
          }
      }
  }

  /**
   *  ByteEagerRedo write (read-only context)
   */
  void ByteEagerRedoWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      uint32_t tries = 0;
      bytelock_t* lock = get_bytelock(addr);

      // get the write lock, with timeout
      while (!bcas32(&(lock->owner), 0u, tx->id))
          if (++tries > BYTELOCK_ACQUIRE_TIMEOUT)
              tmabort();

      // log the lock, drop any read locks I have
      tx->w_bytelocks.insert(lock);
      lock->reader[tx->id-1] = 0;

      // wait (with timeout) for readers to drain out
      // (read 4 bytelocks at a time)
      volatile uint32_t* lock_alias = (volatile uint32_t*)&lock->reader[0];
      for (int i = 0; i < 15; ++i) {
          tries = 0;
          while (lock_alias[i] != 0)
              if (++tries > BYTELOCK_DRAIN_TIMEOUT)
                  tmabort();
      }

      // record in redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      OnFirstWrite(tx, ByteEagerRedoReadRW, ByteEagerRedoWriteRW,
                        ByteEagerRedoCommitRW);
  }

  /**
   *  ByteEagerRedo write (writing context)
   */
  void ByteEagerRedoWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
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
          if (++tries > BYTELOCK_ACQUIRE_TIMEOUT)
              tmabort();

      // log the lock, drop any read locks I have
      tx->w_bytelocks.insert(lock);
      lock->reader[tx->id-1] = 0;

      // wait (with timeout) for readers to drain out
      // (read 4 bytelocks at a time)
      volatile uint32_t* lock_alias = (volatile uint32_t*)&lock->reader[0];
      for (int i = 0; i < 15; ++i) {
          tries = 0;
          while (lock_alias[i] != 0)
              if (++tries > BYTELOCK_DRAIN_TIMEOUT)
                  tmabort();
      }

      // record in redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  ByteEagerRedo unwinder:
   */
  void ByteEagerRedoRollback(STM_ROLLBACK_SIG(tx, except, len))
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
      ResetToRO(tx, ByteEagerRedoReadRO, ByteEagerRedoWriteRO,
                ByteEagerRedoCommitRO);
  }

  /**
   *  ByteEagerRedo in-flight irrevocability:
   */
  bool ByteEagerRedoIrrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  Switch to ByteEagerRedo:
   */
  void ByteEagerRedoOnSwitchTo() { }
}

DECLARE_SIMPLE_METHODS_FROM_NORMAL(ByteEagerRedo)
REGISTER_FGADAPT_ALG(ByteEagerRedo, "ByteEagerRedo", true)

#ifdef STM_ONESHOT_ALG_ByteEagerRedo
DECLARE_AS_ONESHOT(ByteEagerRedo)
#endif
