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
#include "../algs.hpp"
#include "../RedoRAWUtils.hpp"

/**
 *  [mfs] These defines are for tuning backoff behavior... should they be
 *        part of BitLocks.hpp instead?
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

namespace stm
{
  TM_FASTCALL
  void BitEagerRedoWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL
  void BitEagerRedoWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL
  void* BitEagerRedoReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));

  /**
   *  BitEagerRedo begin:
   */
  void BitEagerRedoBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
  }

  /**
   *  BitEagerRedo commit (read-only):
   */
  TM_FASTCALL
  void BitEagerRedoCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // read-only... release read locks
      foreach (BitLockList, i, tx->r_bitlocks)
          (*i)->readers.unsetbit(tx->id-1);

      tx->r_bitlocks.reset();
      OnROCommit(tx);
  }

  /**
   *  BitEagerRedo commit (writing context):
   */
  TM_FASTCALL
  void BitEagerRedoCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // replay redo log
      tx->writes.writeback();
      CFENCE;

      // release write locks, then read locks
      foreach (BitLockList, i, tx->w_bitlocks)
          (*i)->owner = 0;
      foreach (BitLockList, i, tx->r_bitlocks)
          (*i)->readers.unsetbit(tx->id-1);

      // clean-up
      tx->r_bitlocks.reset();
      tx->w_bitlocks.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, BitEagerRedoReadRO, BitEagerRedoWriteRO,
                BitEagerRedoCommitRO);
  }

  /**
   *  BitEagerRedo read (read-only transaction)
   *
   *    As in BitEager, we use timeout for conflict resolution
   */
  TM_FASTCALL
  void* BitEagerRedoReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      uint32_t tries = 0;
      bitlock_t* lock = get_bitlock(addr);

      // do I have a read lock?
      if (lock->readers.getbit(tx->id-1))
          return *addr;

      // log this location
      tx->r_bitlocks.insert(lock);

      // now try to get a read lock
      while (true) {
          // mark my reader bit
          lock->readers.setbit(tx->id-1);

          // if nobody has the write lock, we're done
          if (__builtin_expect(lock->owner == 0, true))
              return *addr;

          // drop read lock, wait (with timeout) for lock release
          lock->readers.unsetbit(tx->id-1);
          while (lock->owner != 0) {
              if (++tries > READ_TIMEOUT)
                  tmabort();
          }
      }
  }

  /**
   *  BitEagerRedo read (writing transaction)
   *
   *    Same as RO case, but if we have the write lock, we can take a fast path
   */
  TM_FASTCALL
  void* BitEagerRedoReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      uint32_t tries = 0;
      bitlock_t* lock = get_bitlock(addr);

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
      if (lock->readers.getbit(tx->id-1))
          return *addr;

      // log this location
      tx->r_bitlocks.insert(lock);

      // now try to get a read lock
      while (true) {
          // mark my reader bit
          lock->readers.setbit(tx->id-1);

          // if nobody has the write lock, we're done
          if (__builtin_expect(lock->owner == 0, true))
              return *addr;

          // drop read lock, wait (with timeout) for lock release
          lock->readers.unsetbit(tx->id-1);
          while (lock->owner != 0) {
              if (++tries > READ_TIMEOUT)
                  tmabort();
          }
      }
  }

  /**
   *  BitEagerRedo write (read-only context)
   *
   *    Lock the location, then put the value in the write log
   */
  TM_FASTCALL
  void BitEagerRedoWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      uint32_t tries = 0;
      bitlock_t* lock = get_bitlock(addr);

      // get the write lock, with timeout
      while (!bcasptr(&(lock->owner), 0u, tx->id))
          if (++tries > ACQUIRE_TIMEOUT)
              tmabort();

      // log the lock, drop any read locks I have
      tx->w_bitlocks.insert(lock);
      lock->readers.unsetbit(tx->id-1);

      // wait (with timeout) for readers to drain out
      // (read one bucket at a time)
      for (unsigned b = 0; b < rrec_t::BUCKETS; ++b) {
          tries = 0;
          while (lock->readers.bits[b])
              if (++tries > DRAIN_TIMEOUT)
                  tmabort();
      }

      // record in redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val,
                                                          mask)));

      OnFirstWrite(tx, BitEagerRedoReadRW, BitEagerRedoWriteRW,
                   BitEagerRedoCommitRW);
  }

  /**
   *  BitEagerRedo write (writing context)
   *
   *    Same as RO case, but with fastpath for repeat writes to same location
   */
  TM_FASTCALL
  void BitEagerRedoWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      uint32_t tries = 0;
      bitlock_t* lock = get_bitlock(addr);

      // If I have the write lock, record in redo log, return
      if (lock->owner == tx->id) {
          tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val,
                                                              mask)));
          return;
      }

      // get the write lock, with timeout
      while (!bcasptr(&(lock->owner), 0u, tx->id))
          if (++tries > ACQUIRE_TIMEOUT)
              tmabort();

      // log the lock, drop any read locks I have
      tx->w_bitlocks.insert(lock);
      lock->readers.unsetbit(tx->id-1);

      // wait (with timeout) for readers to drain out
      // (read one bucket at a time)
      for (unsigned b = 0; b < rrec_t::BUCKETS; ++b) {
          tries = 0;
          while (lock->readers.bits[b])
              if (++tries > DRAIN_TIMEOUT)
                  tmabort();
      }

      // record in redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  BitEagerRedo unwinder:
   */
  void
  BitEagerRedoRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking
      // the branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // release write locks, then read locks
      foreach (BitLockList, i, tx->w_bitlocks)
          (*i)->owner = 0;
      foreach (BitLockList, i, tx->r_bitlocks)
          (*i)->readers.unsetbit(tx->id-1);

      // reset lists
      tx->r_bitlocks.reset();
      tx->w_bitlocks.reset();
      tx->writes.reset();

      // randomized exponential backoff
      exp_backoff(tx);

      PostRollback(tx);
      ResetToRO(tx, BitEagerRedoReadRO, BitEagerRedoWriteRO,
                   BitEagerRedoCommitRO);
  }

  /**
   *  BitEagerRedo in-flight irrevocability:
   */
  bool BitEagerRedoIrrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  Switch to BitEagerRedo:
   *
   *    The only global metadata used by BitEagerRedo is the bitlocks array,
   *    which should be all zeros.
   */
  void BitEagerRedoOnSwitchTo() { }

  /**
   *  BitEagerRedo initialization
   */
  template<>
  void initTM<BitEagerRedo>()
  {
      // set the name
      stms[BitEagerRedo].name      = "BitEagerRedo";

      // set the pointers
      stms[BitEagerRedo].begin     = BitEagerRedoBegin;
      stms[BitEagerRedo].commit    = BitEagerRedoCommitRO;
      stms[BitEagerRedo].read      = BitEagerRedoReadRO;
      stms[BitEagerRedo].write     = BitEagerRedoWriteRO;
      stms[BitEagerRedo].rollback  = BitEagerRedoRollback;
      stms[BitEagerRedo].irrevoc   = BitEagerRedoIrrevoc;
      stms[BitEagerRedo].switcher  = BitEagerRedoOnSwitchTo;
      stms[BitEagerRedo].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_BitEagerRedo
DECLARE_AS_ONESHOT_NORMAL(BitEagerRedo)
#endif
