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
 *  CTokenQ Implementation
 *
 *  CToken using Queue to hand off orders
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"
#include "../cm.hpp"

// for tx->turn.val use
#define NOTDONE 0
#define DONE 1

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace stm
{
  TM_FASTCALL void* CTokenQReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CTokenQReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CTokenQWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CTokenQWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CTokenQCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CTokenQCommitRW(TX_LONE_PARAMETER);
  NOINLINE void CTokenQValidate(TxThread* tx);

  /**
   *  CTokenQ begin:
   */
  void CTokenQBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();

      // get time of last finished txn, to know when to validate
      tx->ts_cache = last_complete.val;

      // reset tx->node[X].val
      tx->node[tx->nn].val = NOTDONE;
  }

  /**
   *  CTokenQ commit (read-only):
   */
  void
  CTokenQCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // reset lists and we are done
      tx->r_orecs.reset();
      OnROCommit(tx);
  }

  /**
   *  CTokenQ commit (writing context):
   *
   *  NB:  Only valid if using pointer-based adaptivity
   */
  void
  CTokenQCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Wait for my turn
      if (tx->node[tx->nn].next != NULL)
          while (tx->node[tx->nn].next->val != DONE);


      // since we have the token, we can validate before getting locks
      if (last_complete.val > tx->ts_cache)
          CTokenQValidate(tx);

      // increment global timestamp and save it to local cache
      tx->order = ++timestamp.val;

      // if we had writes, then aborted, then restarted, and then didn't have
      // writes, we could end up trying to lock a nonexistant write set.
      if (tx->writes.size() != 0) {
          // mark orec and do write back
          foreach (WriteSet, i, tx->writes) {
              orec_t* o = get_orec(i->addr);
              o->v.all = tx->order;
              CFENCE; // WBW
              *i->addr = i->val;
          }
      }
      CFENCE;
      // record last_complete version
      last_complete.val = tx->order;

      // mark self done so that next tx can proceed and reverse tx->nn
      tx->node[tx->nn].val = DONE;
      tx->nn = 1 - tx->nn;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CTokenQReadRO, CTokenQWriteRO, CTokenQCommitRO);
  }

  /**
   *  CTokenQ read (read-only transaction)
   */
  void*
  CTokenQReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // read the location... this is safe since timestamps behave as in Wang's
      // CGO07 paper
      void* tmp = *addr;
      CFENCE; // RBR between dereference and orec check

      // get the orec addr, read the orec's version#
      orec_t* o = get_orec(addr);
      uintptr_t ivt = o->v.all;
      // abort if this changed since the last time I saw someone finish
      //
      // NB: this is a pretty serious tradeoff... it admits false aborts for
      //     the sake of preventing a 'check if locked' test
      if (ivt > tx->ts_cache)
          tmabort();

      // log orec
      tx->r_orecs.insert(o);

      return tmp;
  }

  /**
   *  CTokenQ read (writing transaction)
   */
  void*
  CTokenQReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse the ReadRO barrier, which is adequate here---reduces LOC
      void* val = CTokenQReadRO(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  CTokenQ write (read-only context)
   */
  void
  CTokenQWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // we don't have any writes yet, so we need to add myself to the queue
      do {
          tx->node[tx->nn].next = q;
      } while (!bcasptr(&q, tx->node[tx->nn].next, &(tx->node[tx->nn])));

      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CTokenQReadRW, CTokenQWriteRW, CTokenQCommitRW);
  }

  /**
   *  CTokenQ write (writing context)
   */
  void
  CTokenQWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CTokenQ unwinder:
   */
  void
  CTokenQRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists, but keep any order we acquired
      tx->r_orecs.reset();
      tx->writes.reset();
      // NB: we can't reset pointers here, because if the transaction
      //     performed some writes, then it has an order.  If it has an
      //     order, but restarts and is read-only, then it still must call
      //     CommitRW to finish in-order
      PostRollback(tx);
  }

  /**
   *  CTokenQ in-flight irrevocability:
   */
  bool
  CTokenQIrrevoc(TxThread*)
  {
      UNRECOVERABLE("CTokenQ Irrevocability not yet supported");
      return false;
  }

  /**
   *  CTokenQ validation for CommitRW
   */
  void
  CTokenQValidate(TxThread* tx)
  {
      // check that all reads are valid
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if it has a timestamp of ts_cache or greater, abort
          if (ivt > tx->ts_cache)
              tmabort();
      }
  }

  /**
   *  Switch to CTokenQ:
   *
   */
  void
  CTokenQOnSwitchTo()
  {
      last_complete.val = 0;
      timestamp.val = 0;
  }
}


DECLARE_SIMPLE_METHODS_FROM_NORMAL(CTokenQ)
REGISTER_FGADAPT_ALG(CTokenQ, "CTokenQ", true)

#ifdef STM_ONESHOT_ALG_CTokenQ
DECLARE_AS_ONESHOT(CTokenQ)
#endif
