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
 *  FastlaneSwitch Implementation
 *
 *  Based on J.Wamhoff et.al's paper "FASTLANE: Streamlining Transactions
 *  For Low Thread Counts", TRANSACT'12, FEB.2012, Except that this version
 *  supports master-switching.
 */

/**
 * [mfs] Be sure to address all performance concerns raised in fastlane.cpp
 */

#include "../profiling.hpp"
#include "../algs.hpp"
#include "../RedoRAWUtils.hpp"
#include "../Diagnostics.hpp"

// define atomic operations
//
// [mfs] These should be in abstract_cpu...
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch
#define OR  __sync_or_and_fetch

using stm::TxThread;
using stm::WriteSet;
using stm::OrecList;
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
  // [mfs] These need to be padded
  static const uint32_t MSB = 0x80000000; // Most Important Bit of 32bit interger
  volatile uint32_t cntr = 0;             // Global counter... use timestamp?
  volatile uint32_t helper = 0;           // Helper lock
  volatile uint32_t master = 0;           // Master lock

  struct FastlaneSwitch {
      static void begin(TX_LONE_PARAMETER);
      static TM_FASTCALL void* read_ro(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void* read_turbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_turbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro(TX_LONE_PARAMETER);
      static TM_FASTCALL void commit_rw(TX_LONE_PARAMETER);
      static TM_FASTCALL void commit_turbo(TX_LONE_PARAMETER);

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE bool validate(TxThread* tx);
      static NOINLINE uint32_t WaitForEvenCounter();
      static NOINLINE void EmitWriteSet(TxThread* tx, uint32_t version);
  };

  /**
   *  FastlaneSwitch begin:
   *  Master thread set cntr from even to odd.
   */
  void FastlaneSwitch::begin(TX_LONE_PARAMETER )
  {
      TX_GET_TX_INTERNAL;

      // starts
      tx->allocator.onTxBegin();

      // [mfs] I think this is going to lead to too much synchronization.  It
      //       would be worth trying the following:
      //       1 - read cntr
      //       2 - if cntr MSB 0 and cntr LSB 0, try to cas so that both are 1
      //         -- if fail goto 5 else goto 4
      //       3 - else if cntr MSB 0 and LSB 1, try to cas so that both are 1
      //         -- if succeed, wait for cntr.LSB 0, then set to 1, then goto 4
      //         -- else goto 5
      //       4 - call GoTurbo and return (no WBR is ever needed)
      //       5 - use the slowpath
      //
      //       This would eliminate the need for the MASTER field.

      // Acquire master lock to become master
      if (master == 0 && bcas32(&master, 0, 1)) {

          // master request priority access
          OR(&cntr, MSB);

          // Wait for committing helpers
          while ((cntr & 0x01) != 0)
              spin64();

          // Increment cntr from even to odd
          cntr = (cntr & ~MSB) + 1;
          WBR;

          // master uses turbo mode
          stm::GoTurbo(tx, read_turbo, write_turbo, commit_turbo);
      }

      // helpers get even counter (discard LSD & MSB)
      tx->start_time = cntr & ~1 & ~MSB;

      // Go helper mode
      //
      // [mfs] I don't think this is needed... the prior commit should have
      // reset these to the _ro variants already.
      stm::GoTurbo(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  Fastline: commit_turbo (for master mode):
   */
  void
  FastlaneSwitch::commit_turbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;

      CFENCE; //wbw between write back and change of cntr
      // Only master can write odd cntr, now cntr is even again
      cntr ++;

      // release master lock
      master = 0;
      OnRWCommit(tx);
      ResetToRO(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  FastlaneSwitch commit (read-only):
   *  Read-only transaction commit immediately
   */
  void
  FastlaneSwitch::commit_ro(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // clean up
      tx->r_orecs.reset();

      // set myself done
      OnROCommit(tx);
  }

  /**
   *  FastlaneSwitch commit (writing context):
   *
   */
  void
  FastlaneSwitch::commit_rw(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;

      volatile uint32_t c;

#ifdef OPT1
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
#endif

#ifdef OPT2
      // Only one helper at a time (FIFO lock)
      while (!bcas32(&helper, 0, 1));

      c = WaitForEvenCounter();
      // Pre-validate before acquiring counter
      if (!validate(tx)) {
          CFENCE;
          // Release lock upon failed validation
          helper = 0;
          stm::tmabort();
      }
      // Remember validation time
      uint32_t t = c + 1;

      // Likely commit: try acquiring counter
      while (!bcas32(&cntr, c, c + 1))
          c = WaitForEvenCounter();

      // Check that validation still holds
      if (cntr > t && !validate(tx)) {
          // Release locks upon failed validation
          SUB(&cntr, 1);
          helper = 0;
          stm::tmabort();
      }

      // Write updates to memory
      EmitWriteSet(tx, c+1);
      // Release locks
      ADD(&cntr, 1);
      helper = 0;
#endif
      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  FastlaneSwitch read_turbo, for master mode
   */
  void*
  FastlaneSwitch::read_turbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  FastlaneSwitch read (read-only transaction)
   */
  void*
  FastlaneSwitch::read_ro(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;

      void *val = *addr;
      CFENCE;
      // get orec
      orec_t *o = get_orec(addr);

      // validate read value
      if (o->v.all > tx->start_time)
          stm::tmabort();

      // log orec
      tx->r_orecs.insert(o);
      CFENCE;

      // possibly validate before returning
      foreach(OrecList, i, tx->r_orecs) {
          uintptr_t ivt_inner = (*i)->v.all;
          if (ivt_inner > tx->start_time)
              stm::tmabort();
      }
      return val;
  }

  /**
   *  FastlaneSwitch read (writing transaction)
   */
  void*
  FastlaneSwitch::read_rw(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;

      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse read_ro barrier
      void* val = read_ro(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  FastlaneSwitch write_turbo (in place write for master mode)
   */
  void
  FastlaneSwitch::write_turbo(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      orec_t* o = get_orec(addr);
      o->v.all = cntr; // mark orec
      CFENCE;
      *addr = val; // in place write
  }

  /**
   *  FastlaneSwitch write (read-only context): for first write
   */
  void
  FastlaneSwitch::write_ro(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // get orec
      orec_t *o = get_orec(addr);
      // validate
      if (o->v.all > tx->start_time)
          stm::tmabort();

      // Add to write set
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      stm::OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  FastlaneSwitch write (writing context)
   */
  void
  FastlaneSwitch::write_rw(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // get orec
      orec_t *o = get_orec(addr);
      // validate
      if (o->v.all > tx->start_time)
          stm::tmabort();

      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  FastlaneSwitch unwinder:
   */
  void
  FastlaneSwitch::rollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  FastlaneSwitch in-flight irrevocability:
   */
  bool
  FastlaneSwitch::irrevoc(TxThread*)
  {
      stm::UNRECOVERABLE("FastlaneSwitch Irrevocability not yet supported");
      return false;
  }

  /**
   *  FastlaneSwitch validation for commit:
   *  check that all reads and writes are valid
   */
  bool
  FastlaneSwitch::validate(TxThread* tx)
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
          // If orec changed , return false
          if (o->v.all > tx->start_time) {
              return false;
          }
      }
      return true;
  }

  /**
   *  FastlaneSwitch helper function: wait for even counter
   */
  uint32_t
  FastlaneSwitch::WaitForEvenCounter()
  {
      uint32_t c;
      do {
          c = cntr;
      }while((c & 0x01) != 0);
      return (c & ~MSB);
  }

  /**
   *  FastlaneSwitch helper function: Emit WriteSet
   */
  void
  FastlaneSwitch::EmitWriteSet(TxThread* tx, uint32_t version)
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
   *  Switch to FastlaneSwitch:
   *
   */
  void
  FastlaneSwitch::onSwitchTo()
  {
      cntr = 0;
  }
}

namespace stm {
  /**
   *  FastlaneSwitch initialization
   */
  template<>
  void initTM<FastlaneSwitch>()
  {
      // set the name
      stms[FastlaneSwitch].name      = "FastlaneSwitch";
      // set the pointers
      stms[FastlaneSwitch].begin     = ::FastlaneSwitch::begin;
      stms[FastlaneSwitch].commit    = ::FastlaneSwitch::commit_ro;
      stms[FastlaneSwitch].read      = ::FastlaneSwitch::read_ro;
      stms[FastlaneSwitch].write     = ::FastlaneSwitch::write_ro;
      stms[FastlaneSwitch].rollback  = ::FastlaneSwitch::rollback;
      stms[FastlaneSwitch].irrevoc   = ::FastlaneSwitch::irrevoc;
      stms[FastlaneSwitch].switcher  = ::FastlaneSwitch::onSwitchTo;
      stms[FastlaneSwitch].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_FastlaneSwitch
DECLARE_AS_ONESHOT_TURBO(FastlaneSwitch)
#endif
