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
 *  Fastlane Implementation
 *
 *  Based on J.Wamhoff et.al's paper "FASTLANE: Streamlining Transactions
 *  For Low Thread Counts", TRANSACT'12, FEB.2012
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

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

// Choose your commit implementation, according to the paper, OPT2 is better
//#define OPT1
#define OPT2

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  const uint32_t MSB = 0x80000000;

  // [mfs] Should these be on their own cache lines?  For that matter, why do
  // we need cntr?  Can't we use an existing thing, like timestamp?
  volatile uint32_t cntr = 0;
  // [mfs] helper should probably be on a different line than cntr.
  volatile uint32_t helper = 0;

  struct Fastlane {
      static TM_FASTCALL bool begin(TxThread*);
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_master(STM_READ_SIG(,,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_master(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void commit_ro(TxThread* tx);
      static TM_FASTCALL void commit_rw(TxThread* tx);
      static TM_FASTCALL void commit_master(TxThread* tx);

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE bool validate(TxThread* tx);
      static NOINLINE uint32_t WaitForEvenCounter();
      static NOINLINE void EmitWriteSet(TxThread* tx, uint32_t version);
  };

  /**
   *  Fastlane begin:
   *  Master thread set cntr from even to odd.
   */
  bool
  Fastlane::begin(TxThread* tx)
  {
      tx->allocator.onTxBegin();

      // threads[1] is master
      if (tx->id == 1) {
          // Master request priority access
          OR(&cntr, MSB);

          // Wait for committing helpers
          while ((cntr & 0x01) != 0)
              spin64();

          // Increment cntr from even to odd
          //
          // [mfs] I do not think the WBR is needed.
          cntr = (cntr & ~MSB) + 1;
          WBR;

          // go master mode
          GoTurbo(tx, read_master, write_master, commit_master);
          return true;
      }

      // helpers get even counter (discard LSD & MSB)
      tx->start_time = cntr & ~1 & ~MSB;
      return true;
  }

  /**
   *  Fastline: commit_master:
   */
  void
  Fastlane::commit_master(TxThread* tx)
  {
      CFENCE; //wbw between write back and change of cntr
      // Only master can write odd cntr, now cntr is even again
      cntr++;
      // [mfs] could we use XXX_master here, and somehow avoid the GoTurbo call
      // above?
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  Fastlane commit (read-only):
   *  Read-only transaction commit immediately
   */
  void
  Fastlane::commit_ro(TxThread* tx)
  {
      // clean up
      tx->r_orecs.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  Fastlane commit (writing context):
   *
   */
  void
  Fastlane::commit_rw(TxThread* tx)
  {
      volatile uint32_t c;

#ifdef OPT1
      // Try to acquiring counter
      // Attempt to CAS only after counter seen even
      do {
          // [mfs] It would probably be best to write this code directly, to be
          //       sure that it isn't being inlined, and to be sure that the
          //       control flow is optimal.  We probably should have the
          //       control flow rewritten to optimize for the common case
          //       (e.g., taking first branch directly leads to CAS).
          c = WaitForEvenCounter();
      } while (!bcas32(&cntr, c, c + 1));

      // Release counter upon failed validation
      //
      // [mfs] since this call is always performed, it would be good to ensure
      //       that it is always inlined.
      if (!validate(tx)) {
          // [mfs] if we unioned the counter with an array of volatile bytes,
          // then we could simply clear the lsb of the lowest byte of the
          // counter.  IIRC, the only reason we have an atomic here is to
          // handle the case where the master already did an /or/.  Since this
          // algorithm is already locked-into being x86 only (we don't have an
          // atomic-OR implementation for sparc right now), we could even
          // simply cast a pointer and skip the union (note: endianness bug
          // when we move to SPARC exists even without the cast).
          SUB(&cntr, 1);
          tx->tmabort(tx);
      }

      // [NB] Problem: cntr may be negative number now.
      // in the paper, write orec as cntr, which is wrong,
      // should be c + 1

      // Write updates to memory, mark orec as c + 1
      EmitWriteSet(tx, c + 1);

      // Release counter by making it even again
      //
      // [mfs] As above, we should be able to use a union to avoid needing an
      //       atomic operation here.
      ADD(&cntr, 1);
#endif

#ifdef OPT2
      // Only one helper at a time (FIFO lock)
      //
      // [mfs] this isn't really a FIFO lock, it's just a spin lock.  It should
      //       be rewritten so that the CAS isn't issued unless the value is
      //       zero.  This style of algorithm can lead to lots of unnecessary
      //       bus traffic.
      while (!bcas32(&helper, 0, 1));

      c = WaitForEvenCounter();
      // Pre-validate before acquiring counter
      if (!validate(tx)) {
          CFENCE;
          // Release lock upon failed validation
          helper = 0;
          tx->tmabort(tx);
      }
      // Remember validation time
      uint32_t t = c + 1;

      // Likely commit: try acquiring counter
      while (!bcas32(&cntr, c, c + 1))
          c = WaitForEvenCounter();

      // Check that validation still holds
      if (cntr > t && !validate(tx)) {
          // Release locks upon failed validation
          // [mfs] see above: an atomic SUB is not strictly needed
          SUB(&cntr, 1);
          helper = 0;
          tx->tmabort(tx);
      }

      // Write updates to memory
      EmitWriteSet(tx, c + 1);
      // Release locks
      //
      // [mfs] as above, it isn't really necessary to use an atomic ADD here
      ADD(&cntr, 1);
      helper = 0;
#endif
      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  Fastlane read_master
   */
  void*
  Fastlane::read_master(STM_READ_SIG(tx,addr,))
  {
      return *addr;
  }

  /**
   *  Fastlane read (read-only transaction)
   */
  void*
  Fastlane::read_ro(STM_READ_SIG(tx,addr,))
  {
      void *val = *addr;
      CFENCE;
      // get orec
      orec_t *o = get_orec(addr);

      // validate read value
      if (o->v.all > tx->start_time)
          tx->tmabort(tx);

      // log orec
      tx->r_orecs.insert(o);

      // [mfs] this CFENCE should not be necessary
      CFENCE;

      return val;
  }

  /**
   *  Fastlane read (writing transaction)
   */
  void*
  Fastlane::read_rw(STM_READ_SIG(tx,addr,mask))
  {
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse read_ro barrier
      void* val = read_ro(tx, addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  Fastlane write_master (in place write)
   */
  void
  Fastlane::write_master(STM_WRITE_SIG(tx,addr,val,mask))
  {
      orec_t* o = get_orec(addr);
      // [mfs] strictly speaking, cntr is a volatile, and reading it here means
      //       that there is no hope of caching the value between successive
      //       writes.  However, since this instrumentation is reached across a
      //       function pointer, there is no caching anyway, so it's not too
      //       much of an issue.  However, if we inlined the instrumentation,
      //       we'd see unnecessary overhead.  It might be better to save the
      //       value of cntr in a field of the tx object, so that we can use
      //       that instead.  In fact, doing so would at least ensure no cache
      //       misses due to failed CASes by other threads on the cntr
      //       variable.  Since cntr isn't in its own cache line, this could
      //       actually be very common.
      o->v.all = cntr; // mark orec
      CFENCE;
      *addr = val; // in place write
  }

  /**
   *  Fastlane write (read-only context): for first write
   */
  void
  Fastlane::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // [mfs] I don't understand this line: why would we need to abort for a
      //       WAW conflict?  This should be fine unless we've also read the
      //       location, but in fastlane we validate before grabbing locks, so
      //       this shouldn't be needed.
      // get orec
      orec_t *o = get_orec(addr);
      // validate
      if (o->v.all > tx->start_time)
          tx->tmabort(tx);
      // [mfs] END concern area

      // Add to write set
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  Fastlane write (writing context)
   */
  void
  Fastlane::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // [mfs] as above, I don't think the next three lines of code should be
      // needed
      //
      // get orec
      orec_t *o = get_orec(addr);
      // validate
      if (o->v.all > tx->start_time)
          tx->tmabort(tx);
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  Fastlane unwinder:
   */
  stm::scope_t*
  Fastlane::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();

      return PostRollback(tx);
  }

  /**
   *  Fastlane in-flight irrevocability:
   */
  bool
  Fastlane::irrevoc(TxThread*)
  {
      UNRECOVERABLE("Fastlane Irrevocability not yet supported");
      return false;
  }

  /**
   *  Fastlane validation for commit:
   *  check that all reads and writes are valid
   */
  bool
  Fastlane::validate(TxThread* tx)
  {
      // [mfs] Consider using same technique Luke has been employing elsewhere,
      //       in which we don't have branches in the validation loop.

      // check reads
      foreach (OrecList, i, tx->r_orecs){
          // If orec changed , return false
          if ((*i)->v.all > tx->start_time) {
              return false;
          }
      }

      // [mfs] I don't understand why we need to validate writes.  I think we
      //       can cut this code.

      // check writes
      foreach (WriteSet, j, tx->writes) {
          // get orec
          orec_t* o = get_orec(j->addr);
          // If orec changed , abort
          if (o->v.all > tx->start_time) {
              return false;
          }
      }
      return true;
  }

  /**
   *  Fastlane helper function: wait for even counter
   *
   *  [mfs] We should get rid of this function, in order to ensure optimal
   *        control flow and minimal instruction counts.
   */
  uint32_t
  Fastlane::WaitForEvenCounter()
  {
      uint32_t c;
      do {
          c = cntr;
      }while((c & 0x01) != 0);
      return (c & ~MSB);
  }

  /**
   *  Fastlane helper function: Emit WriteSet
   *
   *  [mfs] Make sure this is always inlined.
   */
  void
  Fastlane::EmitWriteSet(TxThread* tx, uint32_t version)
  {
      foreach (WriteSet, i, tx->writes) {
          // get orec
          orec_t* o = get_orec(i->addr);
          // mark orec
          o->v.all = version;
          CFENCE;
          // do write back
          *i->addr = i->val;
      }
  }

  /**
   *  Switch to Fastlane:
   *
   */
  void
  Fastlane::onSwitchTo()
  {
      cntr = 0;
  }
}

namespace stm {
  /**
   *  Fastlane initialization
   */
  template<>
  void initTM<Fastlane>()
  {
      // set the name
      stms[Fastlane].name      = "Fastlane";
      // set the pointers
      stms[Fastlane].begin     = ::Fastlane::begin;
      stms[Fastlane].commit    = ::Fastlane::commit_ro;
      stms[Fastlane].read      = ::Fastlane::read_ro;
      stms[Fastlane].write     = ::Fastlane::write_ro;
      stms[Fastlane].rollback  = ::Fastlane::rollback;
      stms[Fastlane].irrevoc   = ::Fastlane::irrevoc;
      stms[Fastlane].switcher  = ::Fastlane::onSwitchTo;
      stms[Fastlane].privatization_safe = true;
  }
}

