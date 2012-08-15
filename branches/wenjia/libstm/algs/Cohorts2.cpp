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

#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  TM_FASTCALL bool Cohorts2Validate(TxThread* tx);
  TM_FASTCALL void* Cohorts2ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* Cohorts2ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void Cohorts2WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void Cohorts2WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void Cohorts2CommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void Cohorts2CommitRW(TX_LONE_PARAMETER);

  /**
   *  Cohorts2 begin:
   *  Cohorts2 has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void Cohorts2Begin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();

      while (true) {
          uint32_t c = gatekeeper.val;
          if (!(c & 0x0000FF00)) {
              if (bcas32(&gatekeeper.val, c, c + 1))
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
  Cohorts2CommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx started
      faa32(&gatekeeper.val, -1);

      // clean up
      tx->r_orecs.reset();
      OnROCommit(tx);
  }

  /**
   *  Cohorts2 commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  Cohorts2CommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // increment # ready, decrement # started
      uint32_t old = faa32(&gatekeeper.val, 255);

      // compute my unique order
      // ts_cache stores order of last tx in last cohort
      tx->order = (old >> 8) + tx->ts_cache + 1;

      //printf("old=%d\n",old);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // If I'm not the first one in a cohort to commit, validate reads
      if (tx->order != (intptr_t)(tx->ts_cache + 1))
          if (!Cohorts2Validate(tx)) {
              // mark self as done
              last_complete.val = tx->order;
              // decrement #
              faa32(&gatekeeper.val, -256);
              tmabort();
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
      while (gatekeeper.val & 0x000000FF);

      // do write back
      foreach (WriteSet, i, tx->writes)
          *i->addr = i->val;

      // mark self as done
      last_complete.val = tx->order;

      // decrement # pending
      faa32(&gatekeeper.val, -256);

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, Cohorts2ReadRO, Cohorts2WriteRO, Cohorts2CommitRO);
  }

  /**
   *  Cohorts2 read (read-only transaction)
   */
  void*
  Cohorts2ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // log orec
      tx->r_orecs.insert(get_orec(addr));
      return *addr;
  }

  /**
   *  Cohorts2 read (writing transaction)
   */
  void*
  Cohorts2ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
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
  Cohorts2WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, Cohorts2ReadRW, Cohorts2WriteRW, Cohorts2CommitRW);
  }

  /**
   *  Cohorts2 write (writing context)
   */
  void
  Cohorts2WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  Cohorts2 unwinder:
   */
  void
  Cohorts2Rollback(STM_ROLLBACK_SIG(tx, except, len))
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
  Cohorts2Irrevoc(TxThread*)
  {
      UNRECOVERABLE("Cohorts2 Irrevocability not yet supported");
      return false;
  }

  bool Cohorts2Validate(TxThread* tx)
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
  Cohorts2OnSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = 0;
      gatekeeper.val = 0;
  }

  /**
   *  Cohorts2 initialization
   */
  template<>
  void initTM<Cohorts2>()
  {
      // set the name
      stms[Cohorts2].name      = "Cohorts2";
      // set the pointers
      stms[Cohorts2].begin     = Cohorts2Begin;
      stms[Cohorts2].commit    = Cohorts2CommitRO;
      stms[Cohorts2].read      = Cohorts2ReadRO;
      stms[Cohorts2].write     = Cohorts2WriteRO;
      stms[Cohorts2].rollback  = Cohorts2Rollback;
      stms[Cohorts2].irrevoc   = Cohorts2Irrevoc;
      stms[Cohorts2].switcher  = Cohorts2OnSwitchTo;
      stms[Cohorts2].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_Cohorts2
DECLARE_AS_ONESHOT_NORMAL(Cohorts2)
#endif
