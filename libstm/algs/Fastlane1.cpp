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
 *  Fastlane1 Implementation
 *
 *  Based on J.Wamhoff et.al's paper "FASTLANE: Streamlining Transactions
 *  For Low Thread Counts", TRANSACT'12, FEB.2012
 *
 *  Using Option1 for CommitRW.
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

// define atomic operations
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch
#define OR  __sync_or_and_fetch

namespace stm
{
  // [mfs] Not valid for 64-bit code?
  const uint32_t MSB = 0x80000000;

  TM_FASTCALL void* Fastlane1ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* Fastlane1ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* Fastlane1ReadTurbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void Fastlane1WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void Fastlane1WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void Fastlane1WriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void Fastlane1CommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void Fastlane1CommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void Fastlane1CommitTurbo(TX_LONE_PARAMETER);

  /**
   *  Fastlane1 begin:
   *  Master thread set timestamp.val from even to odd.
   */
  void Fastlane1Begin(TX_LONE_PARAMETER)
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

          // go master mode
          if (!CheckTurboMode(tx, Fastlane1ReadTurbo))
              GoTurbo(tx, Fastlane1ReadTurbo, Fastlane1WriteTurbo, Fastlane1CommitTurbo);
      }

      // helpers get even counter (discard LSD & MSB)
      tx->start_time = timestamp.val & ~1 & ~MSB;
  }

  /**
   *  Fastline: CommitTurbo for master mode
   */
  void
  Fastlane1CommitTurbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      CFENCE; //wbw between write back and change of timestamp.val
      // Only master can write odd timestamp.val, now timestamp.val is even again
      timestamp.val++;
      OnRWCommit(tx);
  }

  /**
   *  Fastlane1 commit (read-only):
   *  Read-only transaction commit immediately
   */
  void
  Fastlane1CommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // clean up
      tx->r_orecs.reset();
      OnROCommit(tx);
  }

  /**
   *  Fastlane1 commit (writing context):
   *
   */
  void
  Fastlane1CommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      uint32_t c;

      // Try to acquiring counter
      // Attempt to CAS only after counter seen even
      // [mfs] e.g., taking first branch directly leads to CAS.
      do {
          while (((c = timestamp.val) & 0x01) != 0);
          c = c & ~MSB;
      } while(!bcas32(&timestamp.val, c, c + 1));

      // Release counter upon failed validation
      //
      // [mfs] if we unioned the counter with an array of volatile bytes,
      // then we could simply clear the lsb of the lowest byte of the
      // counter.  IIRC, the only reason we have an atomic here is to
      // handle the case where the master already did an /or/.  Since this
      // algorithm is already locked-into being x86 only (we don't have an
      // atomic-OR implementation for sparc right now), we could even
      // simply cast a pointer and skip the union (note: endianness bug
      // when we move to SPARC exists even without the cast).
      foreach (OrecList, i, tx->r_orecs)
          // If orec changed , abort
          if ((*i)->v.all > tx->start_time) {
              SUB(&timestamp.val, 1);
              tmabort();
          }

      // Write updates to memory, mark orec as c + 1
      foreach (WriteSet, i, tx->writes) {
          // get orec
          orec_t* o = get_orec(i->addr);
          // mark orec
          o->v.all = c + 1;
          CFENCE;
          // do write back
          *i->addr = i->val;
      }

      // Release counter by making it even again
      //
      // [mfs] As above, we should be able to use a union to avoid needing an
      //       atomic operation here.
      ADD(&timestamp.val, 1);

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, Fastlane1ReadRO, Fastlane1WriteRO, Fastlane1CommitRO);
  }

  /**
   *  Fastlane1 ReadTurbo for master mode
   */
  void*
  Fastlane1ReadTurbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  Fastlane1 read (read-only transaction)
   */
  void*
  Fastlane1ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
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
   *  Fastlane1 read (writing transaction)
   */
  void*
  Fastlane1ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse ReadRO barrier
      void* val = Fastlane1ReadRO(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  Fastlane1 WriteTurbo for master mode (in place write)
   */
  void
  Fastlane1WriteTurbo(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
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
   *  Fastlane1 write (read-only context): for first write
   */
  void
  Fastlane1WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // Add to write set
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, Fastlane1ReadRW, Fastlane1WriteRW, Fastlane1CommitRW);
  }

  /**
   *  Fastlane1 write (writing context)
   */
  void
  Fastlane1WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  Fastlane1 unwinder:
   */
  void
  Fastlane1Rollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  Fastlane1 in-flight irrevocability:
   */
  bool
  Fastlane1Irrevoc(TxThread*)
  {
      UNRECOVERABLE("Fastlane1 Irrevocability not yet supported");
      return false;
  }

  /**
   *  Switch to Fastlane1:
   */
  void
  Fastlane1OnSwitchTo()
  {
      timestamp.val = 0;
  }
}


DECLARE_SIMPLE_METHODS_FROM_TURBO(Fastlane1)
REGISTER_FGADAPT_ALG(Fastlane1, "Fastlane1", true)

#ifdef STM_ONESHOT_ALG_Fastlane1
DECLARE_AS_ONESHOT_TURBO(Fastlane1)
#endif
