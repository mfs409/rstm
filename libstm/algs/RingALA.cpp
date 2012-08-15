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
 *  RingALA Implementation
 *
 *    This is RingSW, extended to support ALA semantics.  We keep a
 *    thread-local filter that unions all write sets that have been posted
 *    since this transaction started, and use that filter to detect ALA
 *    conflicts on every read.
 */

#include "algs.hpp"

namespace stm
{
  TM_FASTCALL void* RingALAReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* RingALAReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void RingALAWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void RingALAWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void RingALACommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void RingALACommitRW(TX_LONE_PARAMETER);
  NOINLINE void RingALAUpdateCF(TxThread*);

  /**
   *  RingALA begin:
   */
  void RingALABegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
      tx->start_time = last_complete.val;
  }

  /**
   *  RingALA commit (read-only):
   */
  void
  RingALACommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // just clear the filters
      tx->rf->clear();
      tx->cf->clear();
      OnROCommit(tx);
  }

  /**
   *  RingALA commit (writing context):
   *
   *    The writer commit algorithm is the same as RingSW
   */
  void
  RingALACommitRW(TX_LONE_PARAMETER)
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
              //
              // NB: in RingSW, we wait for this entry to be complete...
              //     here we skip it, which will require us to repeat the
              //     loop... This decision should be revisited at some point
              if (last_init.val < commit_time)
                  commit_time--;
              // NB: we don't need to union these entries into CF and then
              // intersect CF with RF.  Instead, we can just intersect with
              // RF directly.  This is safe, because RF is guaranteed not to
              // change from here on out.
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

      // copy the bits over (use SSE)
      ring_wf[(commit_time + 1) % RING_ELEMENTS].fastcopy(tx->wf);

      // setting this says "the bits are valid"
      last_init.val = commit_time + 1;

      // we're committed... run redo log, then mark ring entry COMPLETE
      tx->writes.writeback();
      last_complete.val = commit_time + 1;

      // clean up
      tx->writes.reset();
      tx->rf->clear();
      tx->cf->clear();
      tx->wf->clear();
      OnRWCommit(tx);
      ResetToRO(tx, RingALAReadRO, RingALAWriteRO, RingALACommitRO);
  }

  /**
   *  RingALA read (read-only transaction)
   *
   *    RingALA reads are like RingSTM reads, except that we must also verify
   *    that our reads won't result in ALA conflicts
   */
  void*
  RingALAReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // abort if this read would violate ALA
      if (tx->cf->lookup(addr))
          tmabort();

      // read the value from memory, log the address, and validate
      void* val = *addr;
      CFENCE;
      tx->rf->add(addr);
      // get the latest initialized ring entry, return if we've seen it already
      if (__builtin_expect(last_init.val != tx->start_time, false))
          RingALAUpdateCF(tx);
      return val;
  }

  /**
   *  RingALA read (writing transaction)
   */
  void*
  RingALAReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // abort if this read would violate ALA
      if (tx->cf->lookup(addr))
          tmabort();

      // read the value from memory, log the address, and validate
      void* val = *addr;
      CFENCE;
      tx->rf->add(addr);
      // get the latest initialized ring entry, return if we've seen it already
      if (__builtin_expect(last_init.val != tx->start_time, false))
          RingALAUpdateCF(tx);

      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  RingALA write (read-only context)
   */
  void
  RingALAWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // buffer the write and update the filter
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
      OnFirstWrite(tx, RingALAReadRW, RingALAWriteRW, RingALACommitRW);
  }

  /**
   *  RingALA write (writing context)
   */
  void
  RingALAWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      tx->wf->add(addr);
  }

  /**
   *  RingALA unwinder:
   */
  void
  RingALARollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset lists and filters
      tx->rf->clear();
      tx->cf->clear();
      if (tx->writes.size()) {
          tx->writes.reset();
          tx->wf->clear();
      }
      PostRollback(tx);
      ResetToRO(tx, RingALAReadRO, RingALAWriteRO, RingALACommitRO);
  }

  /**
   *  RingALA in-flight irrevocability:
   *
   *  NB: RingALA actually **must** use abort-and-restart to preserve ALA.
   */
  bool RingALAIrrevoc(TxThread*) { return false; }

  /**
   *  RingALA validation
   *
   *    For every new filter, add it to the conflict filter (cf).  Then intersect
   *    the read filter with the conflict filter to identify ALA violations.
   */
  void
  RingALAUpdateCF(TxThread* tx)
  {
      // get latest entry
      uintptr_t my_index = last_init.val;

      // add all new entries to cf
      for (uintptr_t i = my_index; i >= tx->start_time + 1; i--)
          tx->cf->unionwith(ring_wf[i % RING_ELEMENTS]);

      CFENCE;
      // detect ring rollover: start.ts must not have changed
      if (timestamp.val > (tx->start_time + RING_ELEMENTS))
          tmabort();

      // now intersect my rf with my cf
      if (tx->rf->intersect(tx->cf))
          tmabort();

      // wait for newest entry to be writeback-complete before returning
      while (last_complete.val < my_index)
          spin64();

      // ensure this tx doesn't look at this entry again
      tx->start_time = my_index;
  }

  /**
   *  Switch to RingALA:
   *
   *    It really doesn't matter *where* in the ring we start.  What matters is
   *    that the timestamp, last_init, and last_complete are equal.
   */
  void
  RingALAOnSwitchTo()
  {
      last_init.val = timestamp.val;
      last_complete.val = last_init.val;
  }
}

DECLARE_SIMPLE_METHODS_FROM_NORMAL(RingALA)
REGISTER_FGADAPT_ALG(RingALA, "RingALA", true)

#ifdef STM_ONESHOT_ALG_RingALA
DECLARE_AS_ONESHOT_NORMAL(RingALA)
#endif
