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

#include "profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

using stm::TxThread;
using stm::last_complete;
using stm::timestamp;
using stm::WriteSet;
using stm::OrecList;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::orec_t;
using stm::get_orec;
using stm::cohorts_node_t;

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

  struct CTokenQ {
      static TM_FASTCALL bool begin(TxThread*);
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void commit_ro(TxThread* tx);
      static TM_FASTCALL void commit_rw(TxThread* tx);

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE void validate(TxThread* tx);
  };

  /**
   *  CTokenQ begin:
   */
  bool
  CTokenQ::begin(TxThread* tx)
  {
      tx->allocator.onTxBegin();

      // get time of last finished txn, to know when to validate
      tx->ts_cache = last_complete.val;

      // reset tx->turnX.val
      if (tx->status == ONE)
          tx->turn1.val = NOTDONE;
      else
          tx->turn2.val = NOTDONE;

      return false;
  }

  /**
   *  CTokenQ commit (read-only):
   */
  void
  CTokenQ::commit_ro(TxThread* tx)
  {
      // reset lists and we are done
      tx->r_orecs.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  CTokenQ commit (writing context):
   *
   *  NB:  Only valid if using pointer-based adaptivity
   */
  void
  CTokenQ::commit_rw(TxThread* tx)
  {
      // Wait for my turn
      if (tx->status == ONE && tx->turn1.next != NULL)
          while (tx->turn1.next->val != DONE);
      if (tx->status == TWO && tx->turn2.next != NULL)
          while (tx->turn2.next->val != DONE);

      // since we have the token, we can validate before getting locks
      if (last_complete.val > tx->ts_cache)
          validate(tx);

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
      // record last_complete version
      last_complete.val = tx->order;

      // mark self done so that next tx can proceed and reverse tx->status
      if (tx->status == ONE) {
          tx->turn1.val = DONE;
          tx->status = TWO;
      }
      else {
          tx->turn2.val = DONE;
          tx->status = ONE;
      }

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CTokenQ read (read-only transaction)
   */
  void*
  CTokenQ::read_ro(STM_READ_SIG(tx,addr,))
  {
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
          tx->tmabort(tx);

      // log orec
      tx->r_orecs.insert(o);

      return tmp;
  }

  /**
   *  CTokenQ read (writing transaction)
   */
  void*
  CTokenQ::read_rw(STM_READ_SIG(tx,addr,mask))
  {
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse the ReadRO barrier, which is adequate here---reduces LOC
      void* val = read_ro(tx, addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  CTokenQ write (read-only context)
   */
  void
  CTokenQ::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // we don't have any writes yet, so we need to add myself to the queue
      if (tx->status == ONE)
          do {
              tx->turn1.next = q;
          } while (!bcasptr(&q, tx->turn1.next, &(tx->turn1)));
      else
          do {
              tx->turn2.next = q;
          } while (!bcasptr(&q, tx->turn2.next, &(tx->turn2)));

      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  CTokenQ write (writing context)
   */
  void
  CTokenQ::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CTokenQ unwinder:
   */
  stm::scope_t*
  CTokenQ::rollback(STM_ROLLBACK_SIG(tx, except, len))
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
      //     commit_rw to finish in-order
      return PostRollback(tx);
  }

  /**
   *  CTokenQ in-flight irrevocability:
   */
  bool
  CTokenQ::irrevoc(TxThread*)
  {
      UNRECOVERABLE("CTokenQ Irrevocability not yet supported");
      return false;
  }

  /**
   *  CTokenQ validation for commit_rw
   */
  void
  CTokenQ::validate(TxThread* tx)
  {
      // check that all reads are valid
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if it has a timestamp of ts_cache or greater, abort
          if (ivt > tx->ts_cache)
              tx->tmabort(tx);
      }
  }

  /**
   *  Switch to CTokenQ:
   *
   */
  void
  CTokenQ::onSwitchTo()
  {
      last_complete.val = 0;
      timestamp.val = 0;
  }
}

namespace stm {
  /**
   *  CTokenQ initialization
   */
  template<>
  void initTM<CTokenQ>()
  {
      // set the name
      stms[CTokenQ].name      = "CTokenQ";
      // set the pointers
      stms[CTokenQ].begin     = ::CTokenQ::begin;
      stms[CTokenQ].commit    = ::CTokenQ::commit_ro;
      stms[CTokenQ].read      = ::CTokenQ::read_ro;
      stms[CTokenQ].write     = ::CTokenQ::write_ro;
      stms[CTokenQ].rollback  = ::CTokenQ::rollback;
      stms[CTokenQ].irrevoc   = ::CTokenQ::irrevoc;
      stms[CTokenQ].switcher  = ::CTokenQ::onSwitchTo;
      stms[CTokenQ].privatization_safe = true;
  }
}

