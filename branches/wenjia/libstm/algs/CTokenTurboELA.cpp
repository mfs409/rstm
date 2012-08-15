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
 *  CTokenTurboELA Implementation
 *
 *    This code is like CToken, except we aggressively check if a thread is the
 *    'oldest', and if it is, we switch to an irrevocable 'turbo' mode with
 *    in-place writes and no validation.
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace stm
{
  TM_FASTCALL void* CTokenTurboELAReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CTokenTurboELAReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* CTokenTurboELAReadTurbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void CTokenTurboELAWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CTokenTurboELAWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CTokenTurboELAWriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void CTokenTurboELACommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void CTokenTurboELACommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void CTokenTurboELACommitTurbo(TX_LONE_PARAMETER);
  NOINLINE void CTokenTurboELAValidate(TxThread*, uintptr_t finish_cache);

  /**
   *  CTokenTurboELA begin:
   */
  void CTokenTurboELABegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();

      // get time of last finished txn
      tx->ts_cache = last_complete.val;

      // switch to turbo mode?
      //
      // NB: this only applies to transactions that aborted after doing a write
      if (tx->ts_cache == ((uintptr_t)tx->order - 1))
          GoTurbo(tx, CTokenTurboELAReadTurbo, CTokenTurboELAWriteTurbo,
                  CTokenTurboELACommitTurbo);
  }

  /**
   *  CTokenTurboELA commit (read-only):
   */
  void
  CTokenTurboELACommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->r_orecs.reset();
      tx->order = -1;
      OnROCommit(tx);
  }

  /**
   *  CTokenTurboELA commit (writing context):
   *
   *  Only valid with pointer-based adaptivity
   */
  void
  CTokenTurboELACommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // we need to transition to fast here, but not till our turn
      while (last_complete.val != ((uintptr_t)tx->order - 1)) {
          // check if an adaptivity event necessitates that we abort to change
          // modes
          if (tmbegin != CTokenTurboELABegin)
              tmabort();
      }
      // validate
      //
      // [mfs] Should we use Luke's technique to get the branches out of the
      //       loop?
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if it has a timestamp of ts_cache or greater, abort
          if (ivt > tx->ts_cache)
              tmabort();
      }
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

      // set status to committed...
      tx->order = -1;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CTokenTurboELAReadRO, CTokenTurboELAWriteRO,
                CTokenTurboELACommitRO);
  }

  /**
   *  CTokenTurboELA commit (turbo mode):
   */
  void
  CTokenTurboELACommitTurbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      CFENCE; // wbw between writeback and last_complete.val update
      last_complete.val = tx->order;

      // set status to committed...
      tx->order = -1;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, CTokenTurboELAReadRO, CTokenTurboELAWriteRO,
                CTokenTurboELACommitRO);
  }

  /**
   *  CTokenTurboELA read (read-only transaction)
   */
  void*
  CTokenTurboELAReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
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

      // possibly validate before returning.
      //
      // [mfs] Polling like this is necessary for privatization safety, but
      //       otherwise we could cut it out, since we know we're RO and hence
      //       not going to be able to switch to turbo mode
      if (last_complete.val > tx->ts_cache) {
          // [mfs] Should outline this code since it is unlikely
          uintptr_t finish_cache = last_complete.val;
          // [mfs] again: use Luke's trick to reduce this cost.  We're not in a
          //       critical section, so doing so can't hurt other transactions
          foreach (OrecList, i, tx->r_orecs) {
              // read this orec
              uintptr_t ivt_inner = (*i)->v.all;
              // if it has a timestamp of ts_cache or greater, abort
              if (ivt_inner > tx->ts_cache)
                  tmabort();
          }
          // now update the ts_cache to remember that at this time, we were
          // still valid
          tx->ts_cache = finish_cache;
      }
      return tmp;
  }

  /**
   *  CTokenTurboELA read (writing transaction)
   */
  void*
  CTokenTurboELAReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      void* tmp = *addr;
      REDO_RAW_CLEANUP(tmp, found, log, mask);
      CFENCE; // RBR between dereference and orec check

      // get the orec addr, read the orec's version#
      orec_t* o = get_orec(addr);
      uintptr_t ivt = o->v.all;
      // abort if this changed since the last time I saw someone finish
      if (ivt > tx->ts_cache)
          tmabort();

      // log orec
      tx->r_orecs.insert(o);

      // validate, and if we have writes, then maybe switch to fast mode
      if (last_complete.val > tx->ts_cache)
          CTokenTurboELAValidate(tx, last_complete.val);
      return tmp;
  }

  /**
   *  CTokenTurboELA read (read-turbo mode)
   */
  void*
  CTokenTurboELAReadTurbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CTokenTurboELA write (read-only context)
   */
  void
  CTokenTurboELAWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // we don't have any writes yet, so we need to get an order here
      tx->order = 1 + faiptr(&timestamp.val);

      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      OnFirstWrite(tx, CTokenTurboELAReadRW, CTokenTurboELAWriteRW, CTokenTurboELACommitRW);

      // go turbo?
      //
      // NB: we test this on first write, but not subsequent writes, because up
      //     until now we didn't have an order, and thus weren't allowed to use
      //     turbo mode
      CTokenTurboELAValidate(tx, last_complete.val);
  }

  /**
   *  CTokenTurboELA write (writing context)
   */
  void
  CTokenTurboELAWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CTokenTurboELA write (turbo mode)
   */
  void
  CTokenTurboELAWriteTurbo(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // mark the orec, then update the location
      orec_t* o = get_orec(addr);
      o->v.all = tx->order;
      CFENCE;
      STM_DO_MASKED_WRITE(addr, val, mask);
  }

  /**
   *  CTokenTurboELA unwinder:
   *
   *    NB: self-aborts in Turbo Mode are not supported.  We could add undo
   *        logging to address this, and add it in Pipeline too.
   */
  void
  CTokenTurboELARollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);
      // we cannot be in turbo mode
      if (CheckTurboMode(tx, CTokenTurboELAReadTurbo))
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
   *  CTokenTurboELA in-flight irrevocability:
   */
  bool CTokenTurboELAIrrevoc(TxThread*)
  {
      UNRECOVERABLE("CTokenTurboELA Irrevocability not yet supported");
      return false;
  }

  /**
   *  CTokenTurboELA validation
   */
  void CTokenTurboELAValidate(TxThread* tx, uintptr_t finish_cache)
  {
      // [mfs] There is a performance bug here: we should be looking at the
      //       ts_cache to know if we even need to do this loop.  Consider
      //       single-threaded code: it does a write, it goes to this code, and
      //       then it validates even though it doesn't need to validate, ever!

      if (last_complete.val > tx->ts_cache)
          // [mfs] consider using Luke's trick here
          foreach (OrecList, i, tx->r_orecs) {
              // read this orec
              uintptr_t ivt = (*i)->v.all;
              // if it has a timestamp of ts_cache or greater, abort
              if (ivt > tx->ts_cache)
                  tmabort();
          }

      // now update the finish_cache to remember that at this time, we were
      // still valid
      tx->ts_cache = finish_cache;

      // [mfs] End performance concern

      // and if we are now the oldest thread, transition to fast mode
      if (tx->ts_cache == ((uintptr_t)tx->order - 1)) {
          if (tx->writes.size() != 0) {
              // mark every location in the write set, and perform write-back
              foreach (WriteSet, i, tx->writes) {
                  orec_t* o = get_orec(i->addr);
                  o->v.all = tx->order;
                  CFENCE; // WBW
                  *i->addr = i->val;
              }
              GoTurbo(tx, CTokenTurboELAReadTurbo, CTokenTurboELAWriteTurbo,
                      CTokenTurboELACommitTurbo);
          }
      }
  }

  /**
   *  Switch to CTokenTurboELA:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   *    Also, last_complete must equal timestamp
   *
   *    Also, all threads' order values must be -1
   */
  void
  CTokenTurboELAOnSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = timestamp.val;
      for (uint32_t i = 0; i < threadcount.val; ++i)
          threads[i]->order = -1;
  }
}

DECLARE_SIMPLE_METHODS_FROM_TURBO(CTokenTurboELA)
REGISTER_FGADAPT_ALG(CTokenTurboELA, "CTokenTurboELA", true)

#ifdef STM_ONESHOT_ALG_CTokenTurboELA
DECLARE_AS_ONESHOT_TURBO(CTokenTurboELA)
#endif
