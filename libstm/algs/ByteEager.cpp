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

#include "algs.hpp"
#include "../cm.hpp"

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace stm
{
  TM_FASTCALL void* ByteEagerReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* ByteEagerReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void ByteEagerWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void ByteEagerWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void ByteEagerCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void ByteEagerCommitRW(TX_LONE_PARAMETER);

  /**
   *  ByteEager begin:
   */
  void ByteEagerBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
  }

  /**
   *  ByteEager commit (read-only):
   */
  void ByteEagerCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // read-only... release read locks
      foreach (ByteLockList, i, tx->r_bytelocks)
          (*i)->reader[tx->id-1] = 0;

      tx->r_bytelocks.reset();
      OnROCommit(tx);
  }

  /**
   *  ByteEager commit (writing context):
   */
  void ByteEagerCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // release write locks, then read locks
      foreach (ByteLockList, i, tx->w_bytelocks)
          (*i)->owner = 0;
      foreach (ByteLockList, i, tx->r_bytelocks)
          (*i)->reader[tx->id-1] = 0;

      // clean-up
      tx->r_bytelocks.reset();
      tx->w_bytelocks.reset();
      tx->undo_log.reset();
      OnRWCommit(tx);
      ResetToRO(tx, ByteEagerReadRO, ByteEagerWriteRO, ByteEagerCommitRO);
  }

  /**
   *  ByteEager read (read-only transaction)
   */
  void* ByteEagerReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
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
   *  ByteEager read (writing transaction)
   */
  void* ByteEagerReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      uint32_t tries = 0;
      bytelock_t* lock = get_bytelock(addr);

      // do I have the write lock?
      if (lock->owner == tx->id)
          return *addr;

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
          while (lock->owner != 0)
              if (++tries > BYTELOCK_READ_TIMEOUT)
                  tmabort();
      }
  }

  /**
   *  ByteEager write (read-only context)
   */
  void ByteEagerWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
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

      // add to undo log, do in-place write
      tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
      STM_DO_MASKED_WRITE(addr, val, mask);

      OnFirstWrite(tx, ByteEagerReadRW, ByteEagerWriteRW,
                        ByteEagerCommitRW);
  }

  /**
   *  ByteEager write (writing context)
   */
  void ByteEagerWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      uint32_t tries = 0;
      bytelock_t* lock = get_bytelock(addr);

      // If I have the write lock, add to undo log, do write, return
      if (lock->owner == tx->id) {
          tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
          STM_DO_MASKED_WRITE(addr, val, mask);
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

      // add to undo log, do in-place write
      tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
      STM_DO_MASKED_WRITE(addr, val, mask);
  }

  /**
   *  ByteEager unwinder:
   */
  void ByteEagerRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Undo the writes, while at the same time watching out for the exception
      // object.
      STM_UNDO(tx->undo_log, except, len);

      // release write locks, then read locks
      foreach (ByteLockList, i, tx->w_bytelocks)
          (*i)->owner = 0;
      foreach (ByteLockList, i, tx->r_bytelocks)
          (*i)->reader[tx->id-1] = 0;

      // reset lists
      tx->r_bytelocks.reset();
      tx->w_bytelocks.reset();
      tx->undo_log.reset();

      // randomized exponential backoff
      exp_backoff(tx);

      PostRollback(tx);
      ResetToRO(tx, ByteEagerReadRO, ByteEagerWriteRO, ByteEagerCommitRO);
  }

  /**
   *  ByteEager in-flight irrevocability:
   */
  bool ByteEagerIrrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  Switch to ByteEager:
   */
  void ByteEagerOnSwitchTo() { }
}

DECLARE_SIMPLE_METHODS_FROM_NORMAL(ByteEager)
REGISTER_FGADAPT_ALG(ByteEager, "ByteEager", true)

#ifdef STM_ONESHOT_ALG_ByteEager
DECLARE_AS_ONESHOT_NORMAL(ByteEager)
#endif
