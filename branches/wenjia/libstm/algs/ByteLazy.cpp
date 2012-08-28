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
 *  ByteLazy Implementation
 *
 *    This is an unpublished algorithm.  It is identical to BitLazy, except
 *    that it uses TLRW-style ByteLocks instead of BitLocks.
 */

#include "algs.hpp"

namespace stm
{
  TM_FASTCALL void* ByteLazyReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* ByteLazyReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void ByteLazyWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void ByteLazyWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void ByteLazyCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void ByteLazyCommitRW(TX_LONE_PARAMETER);

  /**
   *  ByteLazy begin:
   */
  void ByteLazyBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
      // mark self as alive
      tx->alive = 1;
  }

  /**
   *  ByteLazy commit (read-only):
   */
  void ByteLazyCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // were there remote aborts?
      if (!tx->alive)
          tmabort();
      CFENCE;

      // release read locks
      foreach (ByteLockList, i, tx->r_bytelocks)
          (*i)->reader[tx->id-1] = 0;

      // clean up
      tx->r_bytelocks.reset();
      OnROCommit(tx);
  }

  /**
   *  ByteLazy commit (writing context):
   *
   *    First, get a lock on every location in the write set.  While locking
   *    locations, the tx will accumulate a list of all transactions with
   *    which it conflicts.  Then the tx will force those transactions to
   *    abort.  If the transaction is still alive at that point, it will redo
   *    its writes, release locks, and clean up.
   */
  void ByteLazyCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // try to lock every location in the write set
      unsigned char accumulator[60] = {0};
      // acquire locks, accumulate victim readers
      foreach (WriteSet, i, tx->writes) {
          // get bytelock, read its version#
          bytelock_t* bl = get_bytelock(i->addr);

          // abort if cannot acquire and haven't locked yet
          if (bl->owner == 0) {
              if (!bcas32(&bl->owner, (uintptr_t)0, tx->my_lock.all))
                  tmabort();

              // log lock
              tx->w_bytelocks.insert(bl);

              // get readers
              // (read 4 bytelocks at a time)
              volatile uint32_t* p1 = (volatile uint32_t*)&accumulator[0];
              volatile uint32_t* p2 = (volatile uint32_t*)&bl->reader[0];
              for (int j = 0; j < 15; ++j)
                  p1[j] |= p2[j];
          }
          else if (bl->owner != tx->my_lock.all) {
              tmabort();
          }
      }

      // take me out of the accumulator
      accumulator[tx->id-1] = 0;
      
      // kill the readers
      for (unsigned char c = 0; c < CACHELINE_BYTES - sizeof(uint32_t); ++c)
          if (accumulator[c] == 1)
	    cas32(&threads[c]->alive, 1u, 0u);

      // were there remote aborts?
      CFENCE;
      if (!tx->alive)
          tmabort();
      CFENCE;

      // we committed... replay redo log
      tx->writes.writeback();
      CFENCE;

      // release read locks, write locks
      foreach (ByteLockList, i, tx->w_bytelocks)
          (*i)->owner = 0;
      foreach (ByteLockList, i, tx->r_bytelocks)
          (*i)->reader[tx->id-1] = 0;

      // remember that this was a commit
      tx->r_bytelocks.reset();
      tx->writes.reset();
      tx->w_bytelocks.reset();
      OnRWCommit(tx);
      ResetToRO(tx, ByteLazyReadRO, ByteLazyWriteRO, ByteLazyCommitRO);
  }

  /**
   *  ByteLazy read (read-only transaction)
   */
  void* ByteLazyReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // first test if we've got a read byte
      bytelock_t* bl = get_bytelock(addr);

      // lock and log if the byte is previously unlocked
      if (bl->reader[tx->id-1] == 0) {
          bl->set_read_byte(tx->id-1);
          // log the lock
          tx->r_bytelocks.insert(bl);
      }

      // if there's a writer, it can't be me since I'm in-flight
      if (bl->owner != 0)
          tmabort();

      // order the read before checking for remote aborts
      void* val = *addr;
      CFENCE;

      if (!tx->alive)
          tmabort();

      return val;
  }

  /**
   *  ByteLazy read (writing transaction)
   */
  void* ByteLazyReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // These are used in REDO_RAW_CLEANUP, so they have to be scoped out
      // here. We expect the compiler to do a good job reordering them when
      // this macro is empty (when word-logging).
      bool found = false;
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));

      // first test if we've got a read byte
      bytelock_t* bl = get_bytelock(addr);

      // lock and log if the byte is previously unlocked
      if (bl->reader[tx->id-1] == 0) {
          bl->set_read_byte(tx->id-1);
          // log the lock
          tx->r_bytelocks.insert(bl);
      } else {
          // if so, we may be a writer (all writes are also reads!)
          // check the log
          found = tx->writes.find(log);
          REDO_RAW_CHECK(found, log, mask);
      }

      // if there's a writer, it can't be me since I'm in-flight
      if (bl->owner != 0)
          tmabort();

      // order the read before checking for remote aborts
      void* val = *addr;
      REDO_RAW_CLEANUP(val, found, log, mask);
      CFENCE;

      if (!tx->alive)
          tmabort();

      return val;
  }

  /**
   *  ByteLazy write (read-only context)
   *
   *    In this implementation, every write is a read during execution, so mark
   *    this location as if it was a read.
   */
  void ByteLazyWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // Record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      // if we don't have a read byte, get one
      bytelock_t* bl = get_bytelock(addr);
      if (bl->reader[tx->id-1] == 0) {
          bl->set_read_byte(tx->id-1);
          // log the lock
          tx->r_bytelocks.insert(bl);
      }

      if (bl->owner)
          tmabort();

      OnFirstWrite(tx, ByteLazyReadRW, ByteLazyWriteRW, ByteLazyCommitRW);
  }

  /**
   *  ByteLazy write (writing context)
   */
  void ByteLazyWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // Record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      // if we don't have a read byte, get one
      bytelock_t* bl = get_bytelock(addr);
      if (bl->reader[tx->id-1] == 0) {
          bl->set_read_byte(tx->id-1);
          // log the lock
          tx->r_bytelocks.insert(bl);
      }

      if (bl->owner)
          tmabort();
  }

  /**
   *  ByteLazy unwinder:
   */
  void ByteLazyRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // release the locks
      foreach (ByteLockList, i, tx->w_bytelocks)
          (*i)->owner = 0;
      foreach (ByteLockList, i, tx->r_bytelocks)
          (*i)->reader[tx->id-1] = 0;

      // clear all lists
      tx->r_bytelocks.reset();
      tx->writes.reset();
      tx->w_bytelocks.reset();

      PostRollback(tx);
      ResetToRO(tx, ByteLazyReadRO, ByteLazyWriteRO, ByteLazyCommitRO);
  }

  /**
   *  ByteLazy in-flight irrevocability:
   */
  bool ByteLazyIrrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  Switch to ByteLazy:
   */
  void ByteLazyOnSwitchTo() { }
}

DECLARE_SIMPLE_METHODS_FROM_NORMAL(ByteLazy)
REGISTER_FGADAPT_ALG(ByteLazy, "ByteLazy", true)

#ifdef STM_ONESHOT_ALG_ByteLazy
DECLARE_AS_ONESHOT(ByteLazy)
#endif
