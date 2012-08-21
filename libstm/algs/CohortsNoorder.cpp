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
 *  CohortsNoorder Implementation
 *
 *  This algs is based on LLT, except that we add cohorts' properties.
 *  But unlike cohorts, we do not give orders at the beginning of any
 *  commits.
 *
 *  [mfs] It might be a good idea to add some internal adaptivity, so that we
 *        can use a simple write set (fixed size vector) when the number of
 *        writes is small, and only switch to the hashtable when the number of
 *        writes gets bigger.  Doing that could potentially make the code much
 *        faster for small transactions.
 *
 *  [mfs] Another question to consider is whether it would be a good idea to
 *        have the different threads take turns acquiring orecs... this would
 *        mean no parallel acquisition, but also no need for BCASPTR
 *        instructions.
 */

#include "algs.hpp"

namespace stm
{
  TM_FASTCALL void* CohortsNoorderReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CohortsNoorderReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CohortsNoorderWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsNoorderWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CohortsNoorderCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CohortsNoorderCommitRW(TX_LONE_PARAMETER);
  NOINLINE void CohortsNoorderValidate(TxThread*);

  /**
   *  CohortsNoorder begin:
   *  At first, every tx can start, until one of the tx is ready to commit.
   *  Then no tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  void CohortsNoorderBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
    S1:
      // wait until everyone is committed
      while (cpending.val != committed.val);

      //before start, increase total number of tx in one cohort
      faiptr(&started.val);

      // [NB] we must double check no one is ready to commit yet!
      if (cpending.val > committed.val){
          faaptr(&started.val, -1);
          goto S1;
      }

      // now start
      tx->allocator.onTxBegin();

      // get a start time
      tx->start_time = timestamp.val;
  }

  /**
   *  CohortsNoorder commit (read-only):
   */
  void
  CohortsNoorderCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // decrease total number of tx
      faaptr(&started.val, -1);

      // read-only, so just reset lists
      tx->r_orecs.reset();
      OnROCommit(tx);
  }

  /**
   *  CohortsNoorder commit (writing context):
   */
  void
  CohortsNoorderCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // increase # of tx waiting to commit
      faiptr(&cpending.val);

      // Wait until every tx is ready to commit
      while (cpending.val < started.val);

      foreach (WriteSet, i, tx->writes) {
          // get orec, read its version#
          orec_t* o = get_orec(i->addr);
          uintptr_t ivt = o->v.all;
          // lock all orecs, unless already locked
          if (ivt <= tx->start_time) {
              // abort if cannot acquire
              if (!bcasptr(&o->v.all, ivt, tx->my_lock.all)) {
                  // Increase total number of committed tx
                  faiptr(&committed.val);
                  // abort
                  tmabort();
              }
              // save old version to o->p, remember that we hold the lock
              o->p = ivt;
              tx->locks.insert(o);
          }
          // else if we don't hold the lock abort
          else if (ivt != tx->my_lock.all) {
              // Increase total number of committed tx
              faiptr(&committed.val);
              // abort
              tmabort();
          }
      }

      // increment the global timestamp since we have writes
      uintptr_t end_time = 1 + faiptr(&timestamp.val);

      // skip validation if nobody else committed
      if (end_time != (tx->start_time + 1))
          CohortsNoorderValidate(tx);

      // write back
      tx->writes.writeback();

      // release locks
      CFENCE;
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = end_time;

      // clean-up
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CohortsNoorderReadRO, CohortsNoorderWriteRO, CohortsNoorderCommitRO);

      // increase total number of committed tx
      faiptr(&committed.val);
  }

  /**
   *  CohortsNoorder read (read-only transaction)
   */
  void*
  CohortsNoorderReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // log orec
      tx->r_orecs.insert(get_orec(addr));
      return *addr;
  }

  /**
   *  CohortsNoorder read (writing transaction)
   */
  void*
  CohortsNoorderReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // log orec
      tx->r_orecs.insert(get_orec(addr));

      void* tmp = *addr;
      // fixup is here to minimize the postvalidation orec read latency
      REDO_RAW_CLEANUP(tmp, found, log, mask);
      return tmp;
  }

  /**
   *  CohortsNoorder write (read-only context)
   */
  void
  CohortsNoorderWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CohortsNoorderReadRW, CohortsNoorderWriteRW, CohortsNoorderCommitRW);
  }

  /**
   *  CohortsNoorder write (writing context)
   */
  void
  CohortsNoorderWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsNoorder unwinder:
   */
  void
  CohortsNoorderRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // release the locks and restore version numbers
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = (*i)->p;

      // undo memory operations, reset lists
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      PostRollback(tx);
      ResetToRO(tx, CohortsNoorderReadRO, CohortsNoorderWriteRO, CohortsNoorderCommitRO);
  }

  /**
   *  CohortsNoorder in-flight irrevocability:
   */
  bool
  CohortsNoorderIrrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  CohortsNoorder validation
   */
  void
  CohortsNoorderValidate(TxThread* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all)) {
              // Increase total number of committed tx
              faiptr(&committed.val);
              // abort
              tmabort();
          }
      }
  }

  /**
   *  Switch to CohortsNoorder:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   */
  void
  CohortsNoorderOnSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
  }
}

DECLARE_SIMPLE_METHODS_FROM_NORMAL(CohortsNoorder)
REGISTER_FGADAPT_ALG(CohortsNoorder, "CohortsNoorder", false)

#ifdef STM_ONESHOT_ALG_CohortsNoorder
DECLARE_AS_ONESHOT(CohortsNoorder)
#endif
