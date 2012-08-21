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
 *  BitLazy Implementation
 *
 *    This is an unpublished STM algorithm.
 *
 *    We use RSTM-style visible reader bitmaps (actually, FairSTM-style
 *    visreader bitmaps), with lazy acquire.  Unlike RSTM, this is a lock-based
 *    (blocking) STM.
 *
 *    During execution, the transaction marks all *reads and writes* as reads,
 *    and then at commit time, it accumulates all potential conflicts, aborts
 *    all conflicting threads, and then does write-back.
 *
 *    Performance is quite bad, due to the CAS on each load, and O(R) CASes
 *    after committing (to release read locks).  It would be interesting to see
 *    how eager acquire fared, if there are any optimizations to the code to
 *    make things less costly, and how TLRW variants compare to this code.
 *    'Atomic or' might be useful, too.
 */

#include "algs.hpp"

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace stm
{
  TM_FASTCALL
  void BitLazyWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL
  void BitLazyWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL
  void* BitLazyReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));

  /**
   *  BitLazy begin:
   */
  void BitLazyBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
      tx->alive = 1;
  }

  /**
   *  BitLazy commit (read-only):
   */
  TM_FASTCALL
  void BitLazyCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // were there remote aborts?
      if (!tx->alive)
          tmabort();
      CFENCE;

      // release read locks
      foreach (BitLockList, i, tx->r_bitlocks)
          (*i)->readers.unsetbit(tx->id-1);

      tx->r_bitlocks.reset();
      OnROCommit(tx);
  }

  /**
   *  BitLazy commit (writing context):
   *
   *    First, get a lock on every location in the write set.  While locking
   *    locations, the tx will accumulate a list of all transactions with which
   *    it conflicts.  Then the tx will force those transactions to abort.  If
   *    the transaction is still alive at that point, it will redo its writes,
   *    release locks, and clean up.
   */
  TM_FASTCALL
  void BitLazyCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // try to lock every location in the write set
      rrec_t accumulator = {{0}};
      // acquire locks, accumulate victim readers
      foreach (WriteSet, i, tx->writes) {
          // get bitlock, read its version#
          bitlock_t* bl = get_bitlock(i->addr);
          // abort if cannot acquire and haven't locked yet
          if (bl->owner == 0) {
              if (!bcasptr(&bl->owner, (uintptr_t)0, tx->my_lock.all))
                  tmabort();
              // log lock
              tx->w_bitlocks.insert(bl);
              // get readers
              accumulator |= bl->readers;
          }
          else if (bl->owner != tx->my_lock.all) {
              tmabort();
          }
      }

      // take me out of the accumulator
      accumulator.bits[(tx->id-1)/(8*sizeof(uintptr_t))] &=
          ~(1lu << ((tx->id-1) % (8*sizeof(uintptr_t))));
      // kill conflicting readers
      for (unsigned b = 0; b < rrec_t::BUCKETS; ++b) {
          if (accumulator.bits[b]) {
              for (unsigned c = 0; c < (8*sizeof(uintptr_t)); c++) {
                  if (accumulator.bits[b] & (1ul << c)) {
                      // need atomic for x86 ordering... WBR insufficient
                      //
                      // NB: This CAS seems very expensive.  We could
                      //     probably use regular writes here, as long as we
                      //     enforce the ordering we need later on, e.g., via
                      //     a phony xchg
                      cas32(&threads[(8*sizeof(uintptr_t))*b+c]->alive,
                            1u, 0u);
                  }
              }
          }
      }

      // were there remote aborts?
      CFENCE;
      if (!tx->alive)
          tmabort();
      CFENCE;

      // we committed... replay redo log
      tx->writes.writeback();
      CFENCE;

      // release read locks, write locks
      foreach (BitLockList, i, tx->w_bitlocks)
          (*i)->owner = 0;
      foreach (BitLockList, i, tx->r_bitlocks)
          (*i)->readers.unsetbit(tx->id-1);

      // remember that this was a commit
      tx->r_bitlocks.reset();
      tx->writes.reset();
      tx->w_bitlocks.reset();
      OnRWCommit(tx);
      ResetToRO(tx, BitLazyReadRO, BitLazyWriteRO, BitLazyCommitRO);
  }

  /**
   *  BitLazy read (read-only transaction)
   *
   *    Must preserve write-before-read ordering between marking self as a reader
   *    and checking for conflicting writers.
   */
  void* BitLazyReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // first test if we've got a read bit
      bitlock_t* bl = get_bitlock(addr);
      if (bl->readers.setif(tx->id-1))
          tx->r_bitlocks.insert(bl);
      // if there's a writer, it can't be me since I'm in-flight
      if (bl->owner)
          tmabort();
      // order the read before checking for remote aborts
      void* val = *addr;
      CFENCE;
      if (!tx->alive)
          tmabort();
      return val;
  }

  /**
   *  BitLazy read (writing transaction)
   *
   *    Same as above, but with a test if this tx has a pending write
   */
  TM_FASTCALL
  void* BitLazyReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // Used by REDO_RAW_CLEANUP so they have to be scoped out here. We assume
      // that the compiler will do a good job when byte-logging isn't enabled in
      // compiling this.
      bool found = false;
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));

      // first test if we've got a read bit
      bitlock_t* bl = get_bitlock(addr);
      if (bl->readers.setif(tx->id-1))
          tx->r_bitlocks.insert(bl);
      // if so, we may be a writer (all writes are also reads!)
      else {
          found = tx->writes.find(log);
          REDO_RAW_CHECK(found, log, mask);
      }
      if (bl->owner)
          tmabort();
      void* val = *addr;
      REDO_RAW_CLEANUP(val, found, log, mask);
      CFENCE;
      if (!tx->alive)
          tmabort();
      return val;
  }

  /**
   *  BitLazy write (read-only context)
   *
   *    Log the write, and then mark the location as if reading.
   */
  void BitLazyWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // Record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      // if we don't have a read bit, get one
      bitlock_t* bl = get_bitlock(addr);
      if (bl->readers.setif(tx->id-1))
          tx->r_bitlocks.insert(bl);
      if (bl->owner)
          tmabort();
      OnFirstWrite(tx, BitLazyReadRW, BitLazyWriteRW, BitLazyCommitRW);
  }

  /**
   *  BitLazy write (writing context)
   */
  TM_FASTCALL
  void BitLazyWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // Record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      // if we don't have a read bit, get one
      bitlock_t* bl = get_bitlock(addr);
      if (bl->readers.setif(tx->id-1))
          tx->r_bitlocks.insert(bl);
      if (bl->owner)
          tmabort();
  }

  /**
   *  BitLazy unwinder:
   */
  void
  BitLazyRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // release the locks
      foreach (BitLockList, i, tx->w_bitlocks)
          (*i)->owner = 0;
      foreach (BitLockList, i, tx->r_bitlocks)
          (*i)->readers.unsetbit(tx->id-1);

      tx->r_bitlocks.reset();
      tx->writes.reset();
      tx->w_bitlocks.reset();

      PostRollback(tx);
      ResetToRO(tx, BitLazyReadRO, BitLazyWriteRO, BitLazyCommitRO);
  }

  /**
   *  BitLazy in-flight irrevocability:
   */
  bool BitLazyIrrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  Switch to BitLazy:
   *
   *    The BitLock array should be all zeroes when we start using this algorithm
   */
  void BitLazyOnSwitchTo() {
  }
}

DECLARE_SIMPLE_METHODS_FROM_NORMAL(BitLazy)
REGISTER_FGADAPT_ALG(BitLazy, "BitLazy", true)

#ifdef STM_ONESHOT_ALG_BitLazy
DECLARE_AS_ONESHOT(BitLazy)
#endif
