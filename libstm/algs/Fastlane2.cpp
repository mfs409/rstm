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
 *  Using Option2 for commit_rw.
 */

#include "../profiling.hpp"
#include "../algs.hpp"
#include "../RedoRAWUtils.hpp"

// define atomic operations
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch
#define OR  __sync_or_and_fetch

using stm::TxThread;
using stm::WriteSet;
using stm::OrecList;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::orec_t;
using stm::get_orec;
using stm::timestamp;
using stm::helper;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  const uint32_t MSB = 0x80000000;

  struct Fastlane2 {
      static void begin();
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,));
      static TM_FASTCALL void* read_master(STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_master(STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro();
      static TM_FASTCALL void commit_rw();
      static TM_FASTCALL void commit_master();

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
  };

  /**
   *  Fastlane2 begin:
   *  Master thread set timestamp.val from even to odd.
   */
  void Fastlane2::begin()
  {
      TxThread* tx = stm::Self;
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
          if (stm::tmread != read_master)
              GoTurbo(tx, read_master, write_master, commit_master);
      }

      // helpers get even counter (discard LSD & MSB)
      tx->start_time = timestamp.val & ~1 & ~MSB;
  }

  /**
   *  Fastline: commit_master:
   */
  void
  Fastlane2::commit_master()
  {
      TxThread* tx = stm::Self;
      CFENCE; //wbw between write back and change of timestamp.val
      // Only master can write odd timestamp.val, now timestamp.val is even again
      timestamp.val++;
      OnReadWriteCommit(tx, read_master, write_master, commit_master);
  }

  /**
   *  Fastlane2 commit (read-only):
   *  Read-only transaction commit immediately
   */
  void
  Fastlane2::commit_ro()
  {
      TxThread* tx = stm::Self;
      // clean up
      tx->r_orecs.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  Fastlane2 commit (writing context):
   *
   */
  void
  Fastlane2::commit_rw()
  {
      TxThread* tx = stm::Self;
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
              tx->tmabort();
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
                  tx->tmabort();
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
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  Fastlane2 read_master
   */
  void*
  Fastlane2::read_master(STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  Fastlane2 read (read-only transaction)
   */
  void*
  Fastlane2::read_ro(STM_READ_SIG(addr,))
  {
      TxThread* tx = stm::Self;
      void *val = *addr;
      CFENCE;
      // get orec
      orec_t *o = get_orec(addr);

      // validate read value
      if (o->v.all > tx->start_time)
          tx->tmabort();

      // log orec
      tx->r_orecs.insert(o);

      return val;
  }

  /**
   *  Fastlane2 read (writing transaction)
   */
  void*
  Fastlane2::read_rw(STM_READ_SIG(addr,mask))
  {
      TxThread* tx = stm::Self;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse read_ro barrier
      void* val = read_ro(addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  Fastlane2 write_master (in place write)
   */
  void
  Fastlane2::write_master(STM_WRITE_SIG(addr,val,mask))
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
  Fastlane2::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // Add to write set
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  Fastlane2 write (writing context)
   */
  void
  Fastlane2::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  Fastlane2 unwinder:
   */
  void
  Fastlane2::rollback(STM_ROLLBACK_SIG(tx, except, len))
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
  Fastlane2::irrevoc(TxThread*)
  {
      UNRECOVERABLE("Fastlane2 Irrevocability not yet supported");
      return false;
  }

  /**
   *  Switch to Fastlane2:
   *
   */
  void
  Fastlane2::onSwitchTo()
  {
      timestamp.val = 0;
  }
}

namespace stm {
  /**
   *  Fastlane2 initialization
   */
  template<>
  void initTM<Fastlane2>()
  {
      // set the name
      stms[Fastlane2].name      = "Fastlane2";
      // set the pointers
      stms[Fastlane2].begin     = ::Fastlane2::begin;
      stms[Fastlane2].commit    = ::Fastlane2::commit_ro;
      stms[Fastlane2].read      = ::Fastlane2::read_ro;
      stms[Fastlane2].write     = ::Fastlane2::write_ro;
      stms[Fastlane2].rollback  = ::Fastlane2::rollback;
      stms[Fastlane2].irrevoc   = ::Fastlane2::irrevoc;
      stms[Fastlane2].switcher  = ::Fastlane2::onSwitchTo;
      stms[Fastlane2].privatization_safe = true;
  }
}

