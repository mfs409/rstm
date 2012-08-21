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
 *  CTokenTurboQ Implementation
 *
 *    This code is like CToken, except we aggressively check if a thread is the
 *    'oldest', and if it is, we switch to an irrevocable 'turbo' mode with
 *    in-place writes and no validation.
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

// for tx->turn.val use
#define NOTDONE 0
#define DONE 1
// for tx->status use
#define ABORT 1
#define RESET 0

namespace stm
{
  // global linklist's head
  //
  // [mfs] Need to pad this?
  // [mfs] actually, what is this for?
  struct cohorts_node_t fakenode;

  TM_FASTCALL void* CTokenTurboQReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CTokenTurboQReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CTokenTurboQReadTurbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CTokenTurboQWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CTokenTurboQWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CTokenTurboQWriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CTokenTurboQCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CTokenTurboQCommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void CTokenTurboQCommitTurbo(TX_LONE_PARAMETER);
  NOINLINE void CTokenTurboQValidate(TxThread*);

  /**
   *  CTokenTurboQ begin:
   */
  void CTokenTurboQBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();

      // get time of last finished txn
      tx->ts_cache = last_complete.val;

      // switch to turbo mode?
      //
      // NB: this only applies to transactions that aborted after doing a write
      if (tx->status == ABORT && tx->node[tx->nn].next->val == DONE) {
          // increment timestamp.val, use it as version #
          tx->order = ++timestamp.val;
          GoTurbo(tx, CTokenTurboQReadTurbo, CTokenTurboQWriteTurbo, CTokenTurboQCommitTurbo);
      }
  }

  /**
   *  CTokenTurboQ commit (read-only):
   */
  void
  CTokenTurboQCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->r_orecs.reset();
      OnROCommit(tx);
  }

  /**
   *  CTokenTurboQ commit (writing context):
   *
   *  Only valid with pointer-based adaptivity
   */
  void
  CTokenTurboQCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Wait for my turn
      while (tx->node[tx->nn].next->val != DONE);

      // validate
      if (last_complete.val > tx->ts_cache)
          foreach (OrecList, i, tx->r_orecs) {
              // read this orec
              uintptr_t ivt = (*i)->v.all;
              // if it has a timestamp of ts_cache or greater, abort
              if (ivt > tx->ts_cache) {
                  tx->status = ABORT;
                  tmabort();
              }
          }

      // increment timestamp.val, use it as version #
      tx->order = ++timestamp.val;

      // writeback
      if (tx->writes.size() != 0) {
          // mark every location in the write set, and perform write-back
          foreach (WriteSet, i, tx->writes) {
              orec_t* o = get_orec(i->addr);
              o->v.all = tx->order;
              CFENCE; // WBW
              *i->addr = i->val;
          }
      }

      CFENCE; // wbw between writeback and last_complete.val update
      last_complete.val = tx->order;

      // mark self done so that next tx can proceed
      tx->node[tx->nn].val = DONE;

      // reverse tx->nn (0 <--> 1)
      tx->nn = 1 - tx->nn;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->status = RESET;
      OnRWCommit(tx);
      ResetToRO(tx, CTokenTurboQReadRO, CTokenTurboQWriteRO, CTokenTurboQCommitRO);
  }

  /**
   *  CTokenTurboQ commit (turbo mode):
   */
  void CTokenTurboQCommitTurbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      CFENCE; // wbw between writeback and last_complete.val update
      last_complete.val = tx->order;

      // mark self done so that next tx can proceed
      tx->node[tx->nn].val = DONE;

      // reverse tx->nn(0 <--> 1)
      tx->nn = 1 - tx->nn;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->status = RESET;
      OnRWCommit(tx);
      ResetToRO(tx, CTokenTurboQReadRO, CTokenTurboQWriteRO, CTokenTurboQCommitRO);
  }

  /**
   *  CTokenTurboQ read (read-only transaction)
   */
  void*
  CTokenTurboQReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      void* tmp = *addr;
      CFENCE; // RBR between dereference and orec check

      // get the orec addr, read the orec's version#
      orec_t* o = get_orec(addr);
      uintptr_t ivt = o->v.all;
      // abort if this changed since the last time I saw someone finish
      if (ivt > tx->ts_cache)
          tmabort();

      // log orec
      tx->r_orecs.insert(o);

      return tmp;
  }

  /**
   *  CTokenTurboQ read (writing transaction)
   */
  void*
  CTokenTurboQReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      void* tmp = *addr;
      REDO_RAW_CLEANUP(tmp, found, log, mask);
      CFENCE;// RBR between dereference and orec check

      // get the orec addr, read the orec's version#
      orec_t* o = get_orec(addr);
      uintptr_t ivt = o->v.all;
      // abort if this changed since the last time I saw someone finish
      if (ivt > tx->ts_cache) {
          tx->status = ABORT;
          tmabort();
      }

      // log orec
      tx->r_orecs.insert(o);

      // validate, and if we have writes, then maybe switch to fast mode
      if (last_complete.val > tx->ts_cache)
          CTokenTurboQValidate(tx);
      return tmp;
  }

  /**
   *  CTokenTurboQ read (read-turbo mode)
   */
  void* CTokenTurboQReadTurbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CTokenTurboQ write (read-only context)
   */
  void
  CTokenTurboQWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // reset tx->node[X].val
      tx->node[tx->nn].val = NOTDONE;

      // we don't have any writes yet, so add myself to the queue
      do {
          tx->node[tx->nn].next = q;
      }while (!bcasptr(&q, tx->node[tx->nn].next, &(tx->node[tx->nn])));

      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, CTokenTurboQReadRW, CTokenTurboQWriteRW, CTokenTurboQCommitRW);

      // go turbo?
      //
      // NB: we test this on first write, but not subsequent writes, because up
      //     until now we didn't have an order, and thus weren't allowed to use
      //     turbo mode
      CTokenTurboQValidate(tx);
  }

  /**
   *  CTokenTurboQ write (writing context)
   */
  void
  CTokenTurboQWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CTokenTurboQ write (turbo mode)
   */
  void
  CTokenTurboQWriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // mark the orec, then update the location
      orec_t* o = get_orec(addr);
      o->v.all = tx->order;
      CFENCE;
      STM_DO_MASKED_WRITE(addr, val, mask);
  }

  /**
   *  CTokenTurboQ unwinder:
   *
   *    NB: self-aborts in Turbo Mode are not supported.  We could add undo
   *        logging to address this, and add it in Pipeline too.
   */
  void
  CTokenTurboQRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);
      // we cannot be in turbo mode
      if (CheckTurboMode(tx, CTokenTurboQReadTurbo))
          UNRECOVERABLE("Attempting to abort a turbo-mode transaction!");

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      tx->r_orecs.reset();
      tx->writes.reset();
      // NB: we can't reset pointers here, because if the transaction
      //     performed some writes, then it has an order.  If it has an
      //     order, but restarts and is read-only, then it still must call
      //     CommitRW to finish in-order
      PostRollback(tx);
  }

  /**
   *  CTokenTurboQ in-flight irrevocability:
   */
  bool CTokenTurboQIrrevoc(TxThread*)
  {
      UNRECOVERABLE("CTokenTurboQ Irrevocability not yet supported");
      return false;
  }

  /**
   *  CTokenTurboQ validation
   */
  void CTokenTurboQValidate(TxThread* tx)
  {
      // if we are now the oldest thread, transition to fast mode
      if (tx->node[tx->nn].next->val == DONE) {
          // validate before go fast mode
          if (last_complete.val > tx->ts_cache) {
              foreach (OrecList, i, tx->r_orecs) {
                  // read this orec
                  uintptr_t ivt = (*i)->v.all;
                  // if it has a timestamp of ts_cache or greater, abort
                  if (ivt > tx->ts_cache) {
                      tx->status = ABORT;
                      tmabort();
                  }
              }
          }

          // increment timestamp.val, use it as version #
          tx->order = ++timestamp.val;

          // mark every location in the write set, and perform write-back
          if (tx->writes.size() != 0)
              foreach (WriteSet, i, tx->writes) {
                  orec_t* o = get_orec(i->addr);
                  o->v.all = tx->order;
                  CFENCE; // WBW
                  *i->addr = i->val;
              }
          GoTurbo(tx, CTokenTurboQReadTurbo, CTokenTurboQWriteTurbo, CTokenTurboQCommitTurbo);
          return;
      }

      // If I'm not the oldest thread, do the normal validation
      uint32_t finish_cache = last_complete.val;
      if (finish_cache > tx->ts_cache)
          foreach (OrecList, i, tx->r_orecs) {
              // read this orec
              uintptr_t ivt = (*i)->v.all;
              // if it has a timestamp of ts_cache or greater, abort
              if (ivt > tx->ts_cache) {
                  tx->status = ABORT;
                  tmabort();
              }
          }
      // update ts_cache, indicating I'm still valid up till now
      tx->ts_cache = finish_cache;
  }

  /**
   *  Switch to CTokenTurboQ:
   *
   */
  void
  CTokenTurboQOnSwitchTo()
  {
      last_complete.val = 0;
      timestamp.val = 0;

      // construct a fakenode and connect q to it
      fakenode.val = DONE;
      fakenode.next = NULL;
      q = &fakenode;
  }
}

DECLARE_SIMPLE_METHODS_FROM_TURBO(CTokenTurboQ)
REGISTER_FGADAPT_ALG(CTokenTurboQ, "CTokenTurboQ", true)

#ifdef STM_ONESHOT_ALG_CTokenTurboQ
DECLARE_AS_ONESHOT(CTokenTurboQ)
#endif
