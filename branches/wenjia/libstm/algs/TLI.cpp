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
 *  TLI Implementation
 *
 *    This is a variant of InvalSTM.  We use 1024-bit filters, and standard
 *    "first committer wins" contention management.  What makes this algorithm
 *    interesting is that we replace all the locking from InvalSTM with
 *    optimistic mechanisms.
 */

#include "algs.hpp"

namespace stm
{
  TM_FASTCALL void* TLIReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* TLIReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void TLIWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void TLIWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void TLICommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void TLICommitRW(TX_LONE_PARAMETER);

  /**
   *  TLI begin:
   */
  void TLIBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // mark self as alive
      tx->allocator.onTxBegin();
      tx->alive = 1;
  }

  /**
   *  TLI commit (read-only):
   */
  void
  TLICommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // if the transaction is invalid, abort
      if (__builtin_expect(tx->alive == 2, false))
          tmabort();

      // ok, all is good
      tx->alive = 0;
      tx->rf->clear();
      OnROCommit(tx);
  }

  /**
   *  TLI commit (writing context):
   */
  void
  TLICommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // if the transaction is invalid, abort
      if (__builtin_expect(tx->alive == 2, false))
          tmabort();

      // grab the lock to stop the world
      uintptr_t tmp = timestamp.val;
      while (((tmp&1) == 1) || (!bcasptr(&timestamp.val, tmp, (tmp+1)))) {
          tmp = timestamp.val;
          spin64();
      }

      // double check that we're valid
      if (__builtin_expect(tx->alive == 2,false)) {
          timestamp.val = tmp + 2; // release the lock
          tmabort();
      }

      // kill conflicting transactions
      for (uint32_t i = 0; i < threadcount.val; i++)
          if ((threads[i]->alive == 1) && (tx->wf->intersect(threads[i]->rf)))
              threads[i]->alive = 2;

      // do writeback
      tx->writes.writeback();

      // release the lock and clean up
      tx->alive = 0;
      timestamp.val = tmp+2;
      tx->writes.reset();
      tx->rf->clear();
      tx->wf->clear();
      OnRWCommit(tx);
      ResetToRO(tx, TLIReadRO, TLIWriteRO, TLICommitRO);
  }

  /**
   *  TLI read (read-only transaction)
   *
   *    We do a visible read, so we must write the fact of this read before we
   *    actually access memory.  Then, we must be sure to perform the read during
   *    a period when the world is not stopped for writeback.  Lastly, we must
   *    ensure that we are still valid
   */
  void*
  TLIReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // push address into read filter, ensure ordering w.r.t. the subsequent
      // read of data
      tx->rf->atomic_add(addr);

      // get a consistent snapshot of the value
      while (true) {
          uintptr_t x1 = timestamp.val;
          CFENCE;
          void* val = *addr;
          CFENCE;
          // if the ts was even and unchanged, then the read is valid
          bool ts_ok = !(x1&1) && (timestamp.val == x1);
          CFENCE;
          // if read valid, and we're not killed, return the value
          if ((tx->alive == 1) && ts_ok)
              return val;
          // abort if we're killed
          if (tx->alive == 2)
              tmabort();
      }
  }

  /**
   *  TLI read (writing transaction)
   */
  void*
  TLIReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse the ReadRO barrier, which is adequate here---reduces LOC
      void* val = TLIReadRO(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  TLI write (read-only context)
   */
  void
  TLIWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // buffer the write, update the filter
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
      OnFirstWrite(tx, TLIReadRW, TLIWriteRW, TLICommitRW);
  }

  /**
   *  TLI write (writing context)
   *
   *    Just like the RO case
   */
  void
  TLIWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
  }

  /**
   *  TLI unwinder:
   */
  void
  TLIRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // clear filters and logs
      tx->rf->clear();
      if (tx->writes.size()) {
          tx->writes.reset();
          tx->wf->clear();
      }
      PostRollback(tx);
      ResetToRO(tx, TLIReadRO, TLIWriteRO, TLICommitRO);
  }

  /**
   *  TLI in-flight irrevocability: use abort-and-restart
   */
  bool TLIIrrevoc(TxThread*) { return false; }

  /**
   *  Switch to TLI:
   *
   *    Must be sure the timestamp is not odd.
   */
  void TLIOnSwitchTo()
  {
      if (timestamp.val & 1)
          ++timestamp.val;
  }

  /**
   *  TLI initialization
   */
  template<>
  void initTM<TLI>()
  {
      // set the name
      stms[TLI].name      = "TLI";

      // set the pointers
      stms[TLI].begin     = TLIBegin;
      stms[TLI].commit    = TLICommitRO;
      stms[TLI].read      = TLIReadRO;
      stms[TLI].write     = TLIWriteRO;
      stms[TLI].rollback  = TLIRollback;
      stms[TLI].irrevoc   = TLIIrrevoc;
      stms[TLI].switcher  = TLIOnSwitchTo;
      stms[TLI].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_TLI
DECLARE_AS_ONESHOT_NORMAL(TLI)
#endif
