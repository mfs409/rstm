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
 *  Fastlane2 Implementation
 *
 *  Based on J.Wamhoff et.al's paper "FASTLANE: Streamlining Transactions
 *  For Low Thread Counts", TRANSACT'12, FEB.2012
 *
 *  Using Option2 for CommitRW.
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

// define atomic operations
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch
#define OR  __sync_or_and_fetch

namespace stm
{
  // [mfs] Not 64-bit safe?
  const uint32_t MSB = 0x80000000;

  TM_FASTCALL void*  Fastlane2ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void*  Fastlane2ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void*  Fastlane2ReadTurbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void  Fastlane2WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void  Fastlane2WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void  Fastlane2WriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void  Fastlane2CommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void  Fastlane2CommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void  Fastlane2CommitTurbo(TX_LONE_PARAMETER);

  /**
   *  Fastlane2 begin:
   *  Master thread set timestamp.val from even to odd.
   */
  void Fastlane2Begin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();

      // threads[1] is master
      if (tx->id == 1) {
          // Master request priority access
          OR(&timestamp.val, MSB);

          // Wait for committing helpers
          while ((timestamp.val & 0x01) != 0)
              spin64();

          // Increment timestamp.val from even to odd
          timestamp.val = (timestamp.val & ~MSB) + 1;

          // go turbo mode... this only fires the first time...
          if (!CheckTurboMode(tx, Fastlane2ReadTurbo))
              GoTurbo(tx, Fastlane2ReadTurbo, Fastlane2WriteTurbo, Fastlane2CommitTurbo);
      }

      // helpers get even counter (discard LSD & MSB)
      tx->start_time = timestamp.val & ~1 & ~MSB;
  }

  /**
   *  Fastline: CommitTurbo for master mode:
   */
  void
  Fastlane2CommitTurbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      CFENCE; //wbw between write back and change of timestamp.val
      // Only master can write odd timestamp.val, now timestamp.val is even again
      timestamp.val++;
      OnRWCommit(tx);
  }

  /**
   *  Fastlane2 commit (read-only):
   *  Read-only transaction commit immediately
   */
  void
  Fastlane2CommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // clean up
      tx->r_orecs.reset();
      OnROCommit(tx);
  }

  /**
   *  Fastlane2 commit (writing context):
   *
   */
  void
  Fastlane2CommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      uint32_t c;

      // Only one helper at a time
      while (helper.val == 1 && !bcas32(&helper.val, 0, 1));

      // Wait for even counter
      while (((c = timestamp.val) & 0x01) != 0);
      c = c & ~MSB;

      // Pre-validate before acquiring counter
      foreach (OrecList, i, tx->r_orecs)
          // If orec changed , abort
          if ((*i)->v.all > tx->start_time) {
              CFENCE;
              // Release lock upon failed validation
              helper.val = 0;
              tmabort();
          }

      // Remember validation time
      uint32_t t = c + 1;

      // Likely commit: try acquiring counter
      while (!bcas32(&timestamp.val, c, c + 1)) {
          while (((c = timestamp.val) & 0x01) != 0);
          c = c & ~MSB;
      }

      // Check that validation still holds
      if (timestamp.val > t)
          foreach (OrecList, i, tx->r_orecs)
              // If orec changed , abort
              if ((*i)->v.all > tx->start_time) {
                  // Release locks upon failed validation
                  // [mfs] see above: an atomic SUB is not strictly needed
                  SUB(&timestamp.val, 1);
                  helper.val = 0;
                  tmabort();
              }

      // Write updates to memory
      foreach (WriteSet, i, tx->writes) {
          // get orec
          orec_t* o = get_orec(i->addr);
          // mark orec
          o->v.all = c + 1;
          CFENCE;
          // do write back
          *i->addr = i->val;
      }

      // Release locks
      //
      // [mfs] as above, it isn't really necessary to use an atomic ADD here
      ADD(&timestamp.val, 1);
      helper.val = 0;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, Fastlane2ReadRO, Fastlane2WriteRO, Fastlane2CommitRO);
  }

  /**
   *  Fastlane2 ReadTurbo for master mode
   */
  void*
  Fastlane2ReadTurbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  Fastlane2 read (read-only transaction)
   */
  void*
  Fastlane2ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      void *val = *addr;
      CFENCE;
      // get orec
      orec_t *o = get_orec(addr);

      // validate read value
      if (o->v.all > tx->start_time)
          tmabort();

      // log orec
      tx->r_orecs.insert(o);

      return val;
  }

  /**
   *  Fastlane2 read (writing transaction)
   */
  void*
  Fastlane2ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse ReadRO barrier
      void* val = Fastlane2ReadRO(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  Fastlane2 WriteTurbo (in place write for master mode)
   */
  void
  Fastlane2WriteTurbo(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      orec_t* o = get_orec(addr);
      // [mfs] strictly speaking, timestamp.val is a volatile, and reading it here means
      //       that there is no hope of caching the value between successive
      //       writes.  However, since this instrumentation is reached across a
      //       function pointer, there is no caching anyway, so it's not too
      //       much of an issue.  However, if we inlined the instrumentation,
      //       we'd see unnecessary overhead.  It might be better to save the
      //       value of timestamp.val in a field of the tx object, so that we can use
      //       that instead.  In fact, doing so would at least ensure no cache
      //       misses due to failed CASes by other threads on the timestamp.val
      //       variable.  Since timestamp.val isn't in its own cache line, this could
      //       actually be very common.
      o->v.all = timestamp.val; // mark orec
      CFENCE;
      *addr = val; // in place write
  }

  /**
   *  Fastlane2 write (read-only context): for first write
   */
  void
  Fastlane2WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // Add to write set
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, Fastlane2ReadRW, Fastlane2WriteRW, Fastlane2CommitRW);
  }

  /**
   *  Fastlane2 write (writing context)
   */
  void
  Fastlane2WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  Fastlane2 unwinder:
   */
  void
  Fastlane2Rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();

      PostRollback(tx);
  }

  /**
   *  Fastlane2 in-flight irrevocability:
   */
  bool
  Fastlane2Irrevoc(TxThread*)
  {
      UNRECOVERABLE("Fastlane2 Irrevocability not yet supported");
      return false;
  }

  /**
   *  Switch to Fastlane2:
   *
   */
  void
  Fastlane2OnSwitchTo()
  {
      timestamp.val = 0;
  }
}


DECLARE_SIMPLE_METHODS_FROM_TURBO(Fastlane2)
REGISTER_FGADAPT_ALG(Fastlane2, "Fastlane2", true)

#ifdef STM_ONESHOT_ALG_Fastlane2
DECLARE_AS_ONESHOT_TURBO(Fastlane2)
#endif
