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
#define CAS __sync_val_compare_and_swap
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

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  const uint32_t MSB = 0x80000000;
  volatile uint32_t cntr = 0;

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
      if (tx->id == 1) {
          // master request priority access
          OR(&cntr, MSB);
          // Wait for committing helpers
          while ((cntr & 0x01) != 0)
              spin64();

          // Imcrement cntr from even to odd
          cntr = (cntr & ~MSB) + 1;
          WBR;

          // go master mode
          GoTurbo(tx, read_master, write_master, commit_master);
          return true;
      }

      // helpers get even counter (discard LSD & MSB)
      tx->start_time = cntr & ~1 & ~MSB;

      tx->allocator.onTxBegin();

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
      cntr ++;
      WBR;

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
      // Try to acquiring counter
      // Attempt to CAS only after counter seen even
      do {
          c = WaitForEvenCounter();
      }while (!bcas32(&cntr, c, c+1));

      // Release counter upon failed validation
      if (!validate(tx)) {
          SUB(&cntr, 1);
          tx->tmabort(tx);
      }

      // [NB] Problem: cntr may be negative number now.
      // in the paper, write orec as cntr, which is wrong,
      // should be c+1

      // Write updates to memory, mark orec as c+1
      EmitWriteSet(tx, c+1);

      // Release counter by making it even again
      ADD(&cntr, 1);

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
      // get orec
      orec_t *o = get_orec(addr);
      // validate
      if (o->v.all > tx->start_time)
          tx->tmabort(tx);
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
      // check reads
      foreach (OrecList, i, tx->r_orecs){
          // If orec changed , return false
          if ((*i)->v.all > tx->start_time) {
              return false;
          }
      }

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

