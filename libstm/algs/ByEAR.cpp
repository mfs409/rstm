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
 *  ByEAR Implementation
 *
 *    This code is like ByteEager, except we have redo logs, and we also use an
 *    aggressive contention manager (abort other on conflict).
 */

#include "../cm.hpp"
#include "algs.hpp"

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace stm
{
  TM_FASTCALL void* ByEARReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* ByEARReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void ByEARWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void ByEARWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void ByEARCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void ByEARCommitRW(TX_LONE_PARAMETER);

  /**
   *  ByEAR begin:
   */
  void ByEARBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
      // set self to active
      tx->alive = TX_ACTIVE;
  }

  /**
   *  ByEAR commit (read-only):
   */
  void ByEARCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // read-only... release read locks
      foreach (ByteLockList, i, tx->r_bytelocks)
          (*i)->reader[tx->id-1] = 0;

      tx->r_bytelocks.reset();
      OnROCommit(tx);
  }

  /**
   *  ByEAR commit (writing context):
   */
  void ByEARCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // atomically mark self committed
      if (!bcas32(&tx->alive, TX_ACTIVE, TX_COMMITTED))
          tmabort();

      // we committed... replay redo log
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
      ResetToRO(tx, ByEARReadRO, ByEARWriteRO, ByEARCommitRO);
  }

  /**
   *  ByEAR read (read-only transaction)
   */
  void* ByEARReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      bytelock_t* lock = get_bytelock(addr);

      // do I have a read lock?
      if (lock->reader[tx->id-1] == 0) {
          // first time read, log this location
          tx->r_bytelocks.insert(lock);
          // mark my lock byte
          lock->set_read_byte(tx->id-1);
      }

      if (uint32_t owner = lock->owner) {
          switch (threads[owner-1]->alive) {
            case TX_COMMITTED:
              // abort myself if the owner is writing back
              tmabort();
            case TX_ACTIVE:
              // abort the owner(it's active)
              if (!bcas32(&threads[owner-1]->alive, TX_ACTIVE, TX_ABORTED))
                  tmabort();
              break;
            case TX_ABORTED:
              // if the owner is unwinding, go through and read
              break;
          }
      }

      // do the read
      CFENCE;
      void* result = *addr;
      CFENCE;

      // check for remote abort
      if (tx->alive == TX_ABORTED)
          tmabort();
      return result;
  }

  /**
   *  ByEAR read (writing transaction)
   */
  void* ByEARReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      bytelock_t* lock = get_bytelock(addr);

      // skip instrumentation if I am the writer
      if (lock->owner == tx->id) {
          // [lyj] a liveness check can be inserted but not necessary
          // check the log
          WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
          bool found = tx->writes.find(log);
          REDO_RAW_CHECK(found, log, mask);

          void* val = *addr;
          REDO_RAW_CLEANUP(val, found, log, mask);
          return val;
      }

      // do I have a read lock?
      if (lock->reader[tx->id-1] == 0) {
          // first time read, log this location
          tx->r_bytelocks.insert(lock);
          // mark my lock byte
          lock->set_read_byte(tx->id-1);
      }

      if (uint32_t owner = lock->owner) {
          switch (threads[owner-1]->alive) {
            case TX_COMMITTED:
              // abort myself if the owner is writing back
              tmabort();
            case TX_ACTIVE:
              // abort the owner(it's active)
              if (!bcas32(&threads[owner-1]->alive, TX_ACTIVE, TX_ABORTED))
                  tmabort();
              break;
            case TX_ABORTED:
              // if the owner is unwinding, go through and read
              break;
          }
      }

      // do the read
      CFENCE;
      void* result = *addr;
      CFENCE;

      // check for remote abort
      if (tx->alive == TX_ABORTED)
          tmabort();

      return result;
  }

  /**
   *  ByEAR write (read-only context)
   */
  void ByEARWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      bytelock_t* lock = get_bytelock(addr);

      // abort current owner, wait for release, then acquire
      while (true) {
          // abort the owner if there is one
          if (uint32_t owner = lock->owner)
              cas32(&threads[owner-1]->alive, TX_ACTIVE, TX_ABORTED);
          // try to get ownership
          else if (bcas32(&(lock->owner), 0u, tx->id))
              break;
          // liveness check
          if (tx->alive == TX_ABORTED)
              tmabort();
      }

      // log the lock, drop any read locks I have
      tx->w_bytelocks.insert(lock);
      lock->reader[tx->id-1] = 0;

      // abort active readers
      //
      // [lyj] here we must use the cas to abort the reader, otherwise we
      //       would risk setting the state of a committing transaction to
      //       aborted, which can give readers inconsistent results when they
      //       trying to read while the committer is writing back.
      for (int i = 0; i < 60; ++i)
          if (lock->reader[i] != 0 && threads[i]->alive == TX_ACTIVE)
              if (!bcas32(&threads[i]->alive, TX_ACTIVE, TX_ABORTED))
                  tmabort();

      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      OnFirstWrite(tx, ByEARReadRW, ByEARWriteRW, ByEARCommitRW);
  }

  /**
   *  ByEAR write (writing context)
   */
  void ByEARWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      bytelock_t* lock = get_bytelock(addr);

      // fastpath for repeat writes to the same location
      if (lock->owner == tx->id) {
          tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr,
                                                              val, mask)));
          return;
      }

      // abort current owner, wait for release, then acquire
      while (true) {
          // abort the owner if there is one
          if (uint32_t owner = lock->owner)
              cas32(&threads[owner-1]->alive, TX_ACTIVE, TX_ABORTED);
          // try to get ownership
          else if (bcas32(&(lock->owner), 0u, tx->id))
              break;
          // liveness check
          if (tx->alive == TX_ABORTED)
              tmabort();
      }

      // log the lock, drop any read locks I have
      tx->w_bytelocks.insert(lock);
      lock->reader[tx->id-1] = 0;

      // abort active readers
      for (int i = 0; i < 60; ++i)
          if (lock->reader[i] != 0 && threads[i]->alive == TX_ACTIVE)
              if (!bcas32(&threads[i]->alive, TX_ACTIVE, TX_ABORTED))
                  tmabort();

      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  ByEAR unwinder:
   */
  void ByEARRollback(STM_ROLLBACK_SIG(tx, except, len))
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
      ResetToRO(tx, ByEARReadRO, ByEARWriteRO, ByEARCommitRO);
  }

  /**
   *  ByEAR in-flight irrevocability:
   */
  bool ByEARIrrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  Switch to ByEAR:
   */
  void ByEAROnSwitchTo() { }
}

DECLARE_SIMPLE_METHODS_FROM_NORMAL(ByEAR)
REGISTER_FGADAPT_ALG(ByEAR, "ByEAR", true)

#ifdef STM_ONESHOT_ALG_ByEAR
DECLARE_AS_ONESHOT_NORMAL(ByEAR)
#endif
