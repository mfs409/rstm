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
 *  BitEager Implementation
 *
 *    This STM resembles TLRW, except that it uses an RSTM-style visible reader
 *    bitmap instead of TLRW-style bytelocks.  Like TLRW, we use timeout rather
 *    than remote abort.
 */

#include "../cm.hpp"
#include "algs.hpp"

namespace stm
{
  TM_FASTCALL
  void BitEagerWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL
  void BitEagerWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL
  void* BitEagerReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));

  /**
   *  BitEager begin:
   */
  void BitEagerBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
  }

  /**
   *  BitEager commit (read-only):
   */
  TM_FASTCALL
  void BitEagerCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;

      // read-only... release read locks
      foreach (BitLockList, i, tx->r_bitlocks)
          (*i)->readers.unsetbit(tx->id-1);

      tx->r_bitlocks.reset();
      OnROCommit(tx);
  }

  /**
   *  BitEager commit (writing context):
   */
  TM_FASTCALL
  void BitEagerCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;

      // release write locks, then read locks
      foreach (BitLockList, i, tx->w_bitlocks)
          (*i)->owner = 0;
      foreach (BitLockList, i, tx->r_bitlocks)
          (*i)->readers.unsetbit(tx->id-1);

      // clean-up
      tx->r_bitlocks.reset();
      tx->w_bitlocks.reset();
      tx->undo_log.reset();
      OnRWCommit(tx);
      ResetToRO(tx, BitEagerReadRO, BitEagerWriteRO, BitEagerCommitRO);
  }

  /**
   *  BitEager read (read-only transaction)
   *
   *    This is a timeout-based pessimistic algorithm: try to get a read lock
   *    (there must not be a writer, and WBR issues apply), then read directly
   *    from memory.
   */
  void* BitEagerReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
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
          while (lock->owner != 0)
              if (++tries > BITLOCK_READ_TIMEOUT)
                  tmabort();
      }
  }

  /**
   *  BitEager read (writing transaction)
   *
   *    This is almost identical to the RO case, except that if the caller has
   *    the write lock, we can return immediately.
   */
  TM_FASTCALL
  void* BitEagerReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;

      uint32_t tries = 0;
      bitlock_t* lock = get_bitlock(addr);

      // do I have the write lock?
      if (lock->owner == tx->id)
          return *addr;

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
          while (lock->owner != 0)
              if (++tries > BITLOCK_READ_TIMEOUT)
                  tmabort();
      }
  }

  /**
   *  BitEager write (read-only context)
   *
   *    To write, we acquire the lock via CAS, then wait for all readers to drain
   *    out.
   */
  void BitEagerWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;

      uint32_t tries = 0;
      bitlock_t* lock = get_bitlock(addr);

      // get the write lock, with timeout
      while (!bcasptr(&(lock->owner), 0u, tx->id))
          if (++tries > BITLOCK_ACQUIRE_TIMEOUT)
              tmabort();

      // log the lock, drop any read locks I have
      tx->w_bitlocks.insert(lock);
      lock->readers.unsetbit(tx->id-1);

      // wait (with timeout) for readers to drain out
      // (read one bucket at a time)
      for (unsigned b = 0; b < rrec_t::BUCKETS; ++b) {
          tries = 0;
          while (lock->readers.bits[b])
              if (++tries > BITLOCK_DRAIN_TIMEOUT)
                  tmabort();
      }

      // add to undo log, do in-place write
      tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
      STM_DO_MASKED_WRITE(addr, val, mask);

      OnFirstWrite(tx, BitEagerReadRW, BitEagerWriteRW, BitEagerCommitRW);
  }

  /**
   *  BitEager write (writing context)
   *
   *    This is like the read-only case, except we might already hold the lock.
   */
  void BitEagerWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;

      uint32_t tries = 0;
      bitlock_t* lock = get_bitlock(addr);

      // If I have the write lock, add to undo log, do write, return
      if (lock->owner == tx->id) {
          tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
          STM_DO_MASKED_WRITE(addr, val, mask);
          return;
      }

      // get the write lock, with timeout
      while (!bcasptr(&(lock->owner), 0u, tx->id))
          if (++tries > BITLOCK_ACQUIRE_TIMEOUT)
              tmabort();

      // log the lock, drop any read locks I have
      tx->w_bitlocks.insert(lock);
      lock->readers.unsetbit(tx->id-1);

      // wait (with timeout) for readers to drain out
      // (read one bucket at a time)
      //
      // NB: we're spinning on 32 threads at a time... that might necessitate
      //     re-tuning the backoff parameters, but it's very efficient.
      for (unsigned b = 0; b < rrec_t::BUCKETS; ++b) {
          tries = 0;
          while (lock->readers.bits[b])
              if (++tries > BITLOCK_DRAIN_TIMEOUT)
                  tmabort();
      }

      // add to undo log, do in-place write
      tx->undo_log.insert(UndoLogEntry(STM_UNDO_LOG_ENTRY(addr, *addr, mask)));
      STM_DO_MASKED_WRITE(addr, val, mask);
  }

  /**
   *  BitEager unwinder:
   */
  void BitEagerRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // undo all writes
      STM_UNDO(tx->undo_log, except, len);

      // release write locks, then read locks
      foreach (BitLockList, i, tx->w_bitlocks)
          (*i)->owner = 0;
      foreach (BitLockList, i, tx->r_bitlocks)
          (*i)->readers.unsetbit(tx->id-1);

      // reset lists
      tx->r_bitlocks.reset();
      tx->w_bitlocks.reset();
      tx->undo_log.reset();

      // randomized exponential backoff
      exp_backoff(tx);

      PostRollback(tx);
      ResetToRO(tx, BitEagerReadRO, BitEagerWriteRO, BitEagerCommitRO);
  }

  /**
   *  BitEager in-flight irrevocability:
   */
  bool BitEagerIrrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  Switch to BitEager:
   *
   *    When switching to BitEager, we don't have to do anything special.  The
   *    only global metadata used by BitEager is the bitlocks array, which should
   *    be all zeros.
   */
  void BitEagerOnSwitchTo() { }

  /**
   *  BitEager initialization
   */
  template<>
  void initTM<BitEager>()
  {
      // set the name
      stms[BitEager].name      = "BitEager";

      // set the pointers
      stms[BitEager].begin     = BitEagerBegin;
      stms[BitEager].commit    = BitEagerCommitRO;
      stms[BitEager].read      = BitEagerReadRO;
      stms[BitEager].write     = BitEagerWriteRO;
      stms[BitEager].rollback  = BitEagerRollback;
      stms[BitEager].irrevoc   = BitEagerIrrevoc;
      stms[BitEager].switcher  = BitEagerOnSwitchTo;
      stms[BitEager].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_BitEager
DECLARE_AS_ONESHOT_NORMAL(BitEager)
#endif
