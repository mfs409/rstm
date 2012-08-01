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
 *  Cohorts Implementation
 *
 *  Cohorts has 4 stages. 1) Nobody is running. If anyone starts,
 *  goes to 2) Everybody is running. If anyone is ready to commit,
 *  goes to 3) Every rw tx gets an order, from now on, no one is
 *  allowed to start a tx anymore. When everyone in this cohort is
 *  ready to commit, goes to stage 4)Commit phase. Everyone commits
 *  in an order that given in stage 3. When the last one finishes
 *  its commit, it goes to stage 1. Now tx is allowed to start again.
 */

#include "../profiling.hpp"
#include "../algs.hpp"
#include "../RedoRAWUtils.hpp"

using stm::TxThread;
using stm::last_complete;
using stm::timestamp;
using stm::timestamp_max;
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

  volatile uint32_t cohort_gate = 0;

  NOINLINE bool validate(TxThread* tx);

  struct Cohorts2 {
      static void begin();
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro();
      static TM_FASTCALL void commit_rw();

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
  };

  /**
   *  Cohorts2 begin:
   *  Cohorts2 has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void Cohorts2::begin()
  {
      TxThread* tx = stm::Self;
      tx->allocator.onTxBegin();

      while (true) {
          uint32_t c = cohort_gate;
          if (!(c & 0x0000FF00)) {
              if (bcas32(&cohort_gate, c, c + 1))
                  break;
          }
      }

      // get time of last finished txn
      tx->ts_cache = last_complete.val;
  }

  /**
   *  Cohorts2 commit (read-only):
   */
  void
  Cohorts2::commit_ro()
  {
      TxThread* tx = stm::Self;
      // decrease total number of tx started
      faa32(&cohort_gate, -1);

      // clean up
      tx->r_orecs.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  Cohorts2 commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  Cohorts2::commit_rw()
  {
      TxThread* tx = stm::Self;
      // increment # ready, decrement # started
      uint32_t old = faa32(&cohort_gate, 255);

      // compute my unique order
      // ts_cache stores order of last tx in last cohort
      tx->order = (old >> 8) + tx->ts_cache + 1;

      //printf("old=%d\n",old);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // If I'm not the first one in a cohort to commit, validate reads
      if (tx->order != (intptr_t)(tx->ts_cache + 1))
          if (!validate(tx)) {
              // mark self as done
              last_complete.val = tx->order;
              // decrement #
              faa32(&cohort_gate, -256);
              tx->tmabort();
          }

      // Last one in cohort can skip the orec marking
      if ((old & 0x000000FF) != 1)
          // mark orec
          foreach (WriteSet, i, tx->writes) {
              // get orec
              orec_t* o = get_orec(i->addr);
              // mark orec
              o->v.all = tx->order;
          }

      // Wait until all tx are ready to commit
      while (cohort_gate & 0x000000FF);

      // do write back
      foreach (WriteSet, i, tx->writes)
          *i->addr = i->val;

      // mark self as done
      last_complete.val = tx->order;

      // decrement # pending
      faa32(&cohort_gate, -256);

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  Cohorts2 read (read-only transaction)
   */
  void*
  Cohorts2::read_ro(STM_READ_SIG(addr,))
  {
      TxThread* tx = stm::Self;
      // log orec
      tx->r_orecs.insert(get_orec(addr));
      return *addr;
  }

  /**
   *  Cohorts2 read (writing transaction)
   */
  void*
  Cohorts2::read_rw(STM_READ_SIG(addr,mask))
  {
      TxThread* tx = stm::Self;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // log orec
      tx->r_orecs.insert(get_orec(addr));

      void* tmp = *addr;
      REDO_RAW_CLEANUP(tmp, found, log, mask);
      return tmp;
  }

  /**
   *  Cohorts2 write (read-only context): for first write
   */
  void
  Cohorts2::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      stm::OnFirstWrite(read_rw, write_rw, commit_rw);
  }

  /**
   *  Cohorts2 write (writing context)
   */
  void
  Cohorts2::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  Cohorts2 unwinder:
   */
  void
  Cohorts2::rollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  Cohorts2 in-flight irrevocability:
   */
  bool
  Cohorts2::irrevoc(TxThread*)
  {
      UNRECOVERABLE("Cohorts2 Irrevocability not yet supported");
      return false;
  }

  bool
  validate(TxThread* tx)
  {
      // [mfs] use the luke trick?
      foreach (OrecList, i, tx->r_orecs) {
          // If orec changed, abort
          if ((*i)->v.all > tx->ts_cache)
              return false;
      }
      return true;
  }


  /**
   *  Switch to Cohorts2:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   */
  void
  Cohorts2::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = 0;
  }
}

namespace stm {
  /**
   *  Cohorts2 initialization
   */
  template<>
  void initTM<Cohorts2>()
  {
      // set the name
      stms[Cohorts2].name      = "Cohorts2";
      // set the pointers
      stms[Cohorts2].begin     = ::Cohorts2::begin;
      stms[Cohorts2].commit    = ::Cohorts2::commit_ro;
      stms[Cohorts2].read      = ::Cohorts2::read_ro;
      stms[Cohorts2].write     = ::Cohorts2::write_ro;
      stms[Cohorts2].rollback  = ::Cohorts2::rollback;
      stms[Cohorts2].irrevoc   = ::Cohorts2::irrevoc;
      stms[Cohorts2].switcher  = ::Cohorts2::onSwitchTo;
      stms[Cohorts2].privatization_safe = true;
  }
}
