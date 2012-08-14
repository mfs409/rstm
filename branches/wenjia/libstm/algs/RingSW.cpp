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
 *  RingSW Implementation
 *
 *    This is the "single writer" variant of the RingSTM algorithm, published
 *    by Spear et al. at SPAA 2008.  There are many optimizations, based on the
 *    Fastpath paper by Spear et al. LCPC 2009.
 */

#include "algs.hpp"

namespace stm
{
  TM_FASTCALL void* RingSWReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* RingSWReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void RingSWWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void RingSWWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void RingSWCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void RingSWCommitRW(TX_LONE_PARAMETER);
  NOINLINE void RingSWCheckInflight(TxThread* tx, uintptr_t my_index);

  /**
   *  RingSW begin:
   *
   *    To start a RingSW transaction, we need to find a ring entry that is
   *    writeback-complete.  In the old RingSW, this was hard.  In the new
   *    RingSW, inspired by FastPath, this is easy.
   */
  void RingSWBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
      // start time is when the last txn completed
      tx->start_time = last_complete.val;
  }

  /**
   *  RingSW commit (read-only):
   */
  void
  RingSWCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // clear the filter and we are done
      tx->rf->clear();
      OnROCommit(tx);
  }

  /**
   *  RingSW commit (writing context):
   *
   *    This is the crux of the RingSTM algorithm, and also the foundation for
   *    other livelock-free STMs.  The main idea is that we use a single CAS to
   *    transition a valid transaction from a state in which it is invisible to a
   *    state in which it is logically committed.  This transition stops the
   *    world, while the logically committed transaction replays its writes.
   */
  void
  RingSWCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // get a commit time, but only succeed in the CAS if this transaction
      // is still valid
      uintptr_t commit_time;
      do {
          commit_time = timestamp.val;
          // get the latest ring entry, return if we've seen it already
          if (commit_time != tx->start_time) {
              // wait for the latest entry to be initialized
              while (last_init.val < commit_time)
                  spin64();

              // intersect against all new entries
              for (uintptr_t i = commit_time; i >= tx->start_time + 1; i--)
                  if (ring_wf[i % RING_ELEMENTS].intersect(tx->rf))
                      tmabort();

              // wait for newest entry to be wb-complete before continuing
              while (last_complete.val < commit_time)
                  spin64();

              // detect ring rollover: start.ts must not have changed
              if (timestamp.val > (tx->start_time + RING_ELEMENTS))
                  tmabort();

              // ensure this tx doesn't look at this entry again
              tx->start_time = commit_time;
          }
      } while (!bcasptr(&timestamp.val, commit_time, commit_time + 1));

      // copy the bits over (use SSE, not indirection)
      ring_wf[(commit_time + 1) % RING_ELEMENTS].fastcopy(tx->wf);

      // setting this says "the bits are valid"
      last_init.val = commit_time + 1;

      // we're committed... run redo log, then mark ring entry COMPLETE
      tx->writes.writeback();
      last_complete.val = commit_time + 1;

      // clean up
      tx->writes.reset();
      tx->rf->clear();
      tx->wf->clear();
      OnRWCommit(tx);
      ResetToRO(tx, RingSWReadRO, RingSWWriteRO, RingSWCommitRO);
  }

  /**
   *  RingSW read (read-only transaction)
   */
  void*
  RingSWReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // read the value from memory, log the address, and validate
      void* val = *addr;
      CFENCE;
      tx->rf->add(addr);
      // get the latest initialized ring entry, return if we've seen it already
      uintptr_t my_index = last_init.val;
      if (__builtin_expect(my_index != tx->start_time, false))
          RingSWCheckInflight(tx, my_index);
      return val;
  }

  /**
   *  RingSW read (writing transaction)
   */
  void*
  RingSWReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse the ReadRO barrier, which is adequate here---reduces LOC
      void* val = RingSWReadRO(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  RingSW write (read-only context)
   */
  void
  RingSWWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // buffer the write and update the filter
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
      OnFirstWrite(tx, RingSWReadRW, RingSWWriteRW, RingSWCommitRW);
  }

  /**
   *  RingSW write (writing context)
   */
  void
  RingSWWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
  }

  /**
   *  RingSW unwinder:
   */
  void
  RingSWRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset filters and lists
      tx->rf->clear();
      if (tx->writes.size()) {
          tx->writes.reset();
          tx->wf->clear();
      }
      PostRollback(tx);
      ResetToRO(tx, RingSWReadRO, RingSWWriteRO, RingSWCommitRO);
  }

  /**
   *  RingSW in-flight irrevocability: use abort-and-restart
   */
  bool
  RingSWIrrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  RingSW validation
   *
   *    check the ring for new entries and validate against them
   */
  void
  RingSWCheckInflight(TxThread* tx, uintptr_t my_index)
  {
      // intersect against all new entries
      for (uintptr_t i = my_index; i >= tx->start_time + 1; i--)
          if (ring_wf[i % RING_ELEMENTS].intersect(tx->rf))
              tmabort();

      // wait for newest entry to be writeback-complete before returning
      while (last_complete.val < my_index)
          spin64();

      // detect ring rollover: start.ts must not have changed
      if (timestamp.val > (tx->start_time + RING_ELEMENTS))
          tmabort();

      // ensure this tx doesn't look at this entry again
      tx->start_time = my_index;
  }

  /**
   *  Switch to RingSW:
   *
   *    It really doesn't matter *where* in the ring we start.  What matters is
   *    that the timestamp, last_init, and last_complete are equal.
   */
  void
  RingSWOnSwitchTo()
  {
      last_init.val = timestamp.val;
      last_complete.val = last_init.val;
  }

  /**
   *  RingSW initialization
   */
  template<>
  void initTM<RingSW>()
  {
      // set the name
      stms[RingSW].name      = "RingSW";

      // set the pointers
      stms[RingSW].begin     = RingSWBegin;
      stms[RingSW].commit    = RingSWCommitRO;
      stms[RingSW].read      = RingSWReadRO;
      stms[RingSW].write     = RingSWWriteRO;
      stms[RingSW].rollback  = RingSWRollback;
      stms[RingSW].irrevoc   = RingSWIrrevoc;
      stms[RingSW].switcher  = RingSWOnSwitchTo;
      stms[RingSW].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_RingSW
DECLARE_AS_ONESHOT_NORMAL(RingSW)
#endif
