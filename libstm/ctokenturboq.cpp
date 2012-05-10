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

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"
#include <stm/UndoLog.hpp> // STM_DO_MASKED_WRITE

using stm::TxThread;
using stm::last_complete;
using stm::OrecList;
using stm::WriteSet;
using stm::UNRECOVERABLE;
using stm::orec_t;
using stm::get_orec;
using stm::WriteSetEntry;
using stm::cohorts_node_t;
using stm::timestamp;
using stm::timestamp_max;

// for tx->turn.val use
#define NOTDONE 0
#define DONE 1
// for tx->status use
#define ONE 0
#define TWO 1

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  // global linklist's head
  struct cohorts_node_t* volatile q = NULL;

  struct CTokenTurboQ {
      static TM_FASTCALL bool begin(TxThread*);
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_turbo(STM_READ_SIG(,,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_turbo(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void commit_ro(TxThread*);
      static TM_FASTCALL void commit_rw(TxThread*);
      static TM_FASTCALL void commit_turbo(TxThread*);

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE void validate(TxThread*, uintptr_t finish_cache);
  };

  /**
   *  CTokenTurboQ begin:
   */
  bool
  CTokenTurboQ::begin(TxThread* tx)
  {
      tx->allocator.onTxBegin();

      // get time of last finished txn
      tx->ts_cache = last_complete.val;

      // reset tx->node[X].val
      tx->node[tx->status].val = NOTDONE;

      // switch to turbo mode?
      //
      // NB: this only applies to transactions that aborted after doing a write
      //if (tx->ts_cache == ((uintptr_t)tx->order - 1))
          //GoTurbo(tx, read_turbo, write_turbo, commit_turbo);

      return false;
  }

  /**
   *  CTokenTurboQ commit (read-only):
   */
  void
  CTokenTurboQ::commit_ro(TxThread* tx)
  {
      tx->r_orecs.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  CTokenTurboQ commit (writing context):
   *
   *  Only valid with pointer-based adaptivity
   */
  void
  CTokenTurboQ::commit_rw(TxThread* tx)
  {
      // Wait for my turn
      if (tx->node[tx->status].next != NULL)
          while (tx->node[tx->status].next->val != DONE);

      //while (last_complete.val != ((uintptr_t)tx->order - 1));

      // validate
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if it has a timestamp of ts_cache or greater, abort
          if (ivt > tx->ts_cache)
              tx->tmabort(tx);
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

      // mark self done so that next tx can proceed and reverse tx->status
      tx->node[tx->status].val = DONE;
      tx->status = 1 - tx->status;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CTokenTurboQ commit (turbo mode):
   */
  void
  CTokenTurboQ::commit_turbo(TxThread* tx)
  {
      CFENCE; // wbw between writeback and last_complete.val update
      last_complete.val = tx->order;

      // mark self done so that next tx can proceed and reverse tx->status
      tx->node[tx->status].val = DONE;
      tx->status = 1 - tx->status;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CTokenTurboQ read (read-only transaction)
   */
  void*
  CTokenTurboQ::read_ro(STM_READ_SIG(tx,addr,))
  {
      void* tmp = *addr;
      CFENCE; // RBR between dereference and orec check

      // get the orec addr, read the orec's version#
      orec_t* o = get_orec(addr);
      uintptr_t ivt = o->v.all;
      // abort if this changed since the last time I saw someone finish
      if (ivt > tx->ts_cache)
          tx->tmabort(tx);

      // log orec
      tx->r_orecs.insert(o);

      return tmp;
  }

  /**
   *  CTokenTurboQ read (writing transaction)
   */
  void*
  CTokenTurboQ::read_rw(STM_READ_SIG(tx,addr,mask))
  {
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
          tx->tmabort(tx);

      // log orec
      tx->r_orecs.insert(o);

      // validate, and if we have writes, then maybe switch to fast mode
      if (last_complete.val > tx->ts_cache)
          validate(tx, last_complete.val);
      return tmp;
  }

  /**
   *  CTokenTurboQ read (read-turbo mode)
   */
  void*
  CTokenTurboQ::read_turbo(STM_READ_SIG(,addr,))
  {
      return *addr;
  }

  /**
   *  CTokenTurboQ write (read-only context)
   */
  void
  CTokenTurboQ::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // we don't have any writes yet, so add myself to the queue

      do {
          tx->node[tx->status].next = q;
      }while (!bcasptr(&q, tx->node[tx->status].next, &(tx->node[tx->status])));

      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);

      // go turbo?
      //
      // NB: we test this on first write, but not subsequent writes, because up
      //     until now we didn't have an order, and thus weren't allowed to use
      //     turbo mode
      validate(tx, last_complete.val);
  }

  /**
   *  CTokenTurboQ write (writing context)
   */
  void
  CTokenTurboQ::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CTokenTurboQ write (turbo mode)
   */
  void
  CTokenTurboQ::write_turbo(STM_WRITE_SIG(tx,addr,val,mask))
  {
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
  stm::scope_t*
  CTokenTurboQ::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);
      // we cannot be in turbo mode
      if (CheckTurboMode(tx, read_turbo))
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
      //     commit_rw to finish in-order
      return PostRollback(tx);
  }

  /**
   *  CTokenTurboQ in-flight irrevocability:
   */
  bool CTokenTurboQ::irrevoc(TxThread*)
  {
      UNRECOVERABLE("CTokenTurboQ Irrevocability not yet supported");
      return false;
  }

  /**
   *  CTokenTurboQ validation
   */
  void CTokenTurboQ::validate(TxThread* tx, uintptr_t finish_cache)
  {
      if (last_complete.val > tx->ts_cache)
          foreach (OrecList, i, tx->r_orecs) {
              // read this orec
              uintptr_t ivt = (*i)->v.all;
              // if it has a timestamp of ts_cache or greater, abort
              if (ivt > tx->ts_cache)
                  tx->tmabort(tx);
          }

      // now update the finish_cache to remember that at this time, we were
      // still valid
      tx->ts_cache = finish_cache;
      /*
      // and if we are now the oldest thread, transition to fast mode
      //if (tx->ts_cache == ((uintptr_t)tx->order - 1)) {
      if (tx->node[tx->status].next == NULL || tx->node[tx->status].next->val == DONE){
          // increment timestamp.val, use it as version #
          tx->order = timestamp.val++;
          if (tx->writes.size() != 0) {
              // mark every location in the write set, and perform write-back
              foreach (WriteSet, i, tx->writes) {
                  orec_t* o = get_orec(i->addr);
                  o->v.all = tx->order;
                  CFENCE; // WBW
                  *i->addr = i->val;
              }
              //if (tx->status == ONE && tx->turn1.next != NULL && tx->turn1.next->val != DONE)
              //    printf("%d is go turbo.\n", tx->id);
              GoTurbo(tx, read_turbo, write_turbo, commit_turbo);
          }
      }
      */
  }

  /**
   *  Switch to CTokenTurboQ:
   *
   */
  void
  CTokenTurboQ::onSwitchTo()
  {
      last_complete.val = 0;
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
  }
}

namespace stm {
  /**
   *  CTokenTurboQ initialization
   */
  template<>
  void initTM<CTokenTurboQ>()
  {
      // set the name
      stms[CTokenTurboQ].name      = "CTokenTurboQ";

      // set the pointers
      stms[CTokenTurboQ].begin     = ::CTokenTurboQ::begin;
      stms[CTokenTurboQ].commit    = ::CTokenTurboQ::commit_ro;
      stms[CTokenTurboQ].read      = ::CTokenTurboQ::read_ro;
      stms[CTokenTurboQ].write     = ::CTokenTurboQ::write_ro;
      stms[CTokenTurboQ].rollback  = ::CTokenTurboQ::rollback;
      stms[CTokenTurboQ].irrevoc   = ::CTokenTurboQ::irrevoc;
      stms[CTokenTurboQ].switcher  = ::CTokenTurboQ::onSwitchTo;
      stms[CTokenTurboQ].privatization_safe = true;
  }
}
