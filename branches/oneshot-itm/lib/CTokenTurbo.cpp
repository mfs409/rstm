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
 *  CTokenTurbo Implementation
 *
 *    This code is like CToken, except we aggressively check if a thread is the
 *    'oldest', and if it is, we switch to an irrevocable 'turbo' mode with
 *    in-place writes and no validation.
 */

#include <stdint.h>
#include <iostream>
#include <cassert>
#include <setjmp.h>
#include <unistd.h>
#include "platform.hpp"
#include "WriteSet.hpp"
#include "WBMMPolicy.hpp"
#include "Macros.hpp"
#include "tx.hpp"
#include "adaptivity.hpp"

using namespace stm;

namespace ctokenturbo
{
  /*** The only metadata we need is a single global padded lock ***/
  pad_word_t timestamp = {0};
  pad_word_t last_complete = {0};

  /**
   *  For querying to get the current algorithm name
   */
  const char* tm_getalgname() { return "CTokenTurbo"; }

  /**
   *  CTokenTurbo unwinder:
   *
   *    NB: self-aborts in Turbo Mode are not supported.  We could add undo
   *        logging to address this, and add it in Pipeline too.
   */
  scope_t* rollback(TX* tx)
  {
      ++tx->aborts;

      // we cannot be in turbo mode
      if (tx->turbo) {
          printf("Error: attempting to abort a turbo-mode transaction\n");
          exit(-1);
      }

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
      tx->allocator.onTxAbort();
      tx->nesting_depth = 0;
      scope_t* scope = tx->scope;
      tx->scope = NULL;
      return scope;
  }

  /**
   *  CTokenTurbo validation
   */
  NOINLINE
  void validate(TX* tx, uintptr_t finish_cache)
  {
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if it has a timestamp of ts_cache or greater, abort
          if (ivt > tx->ts_cache)
              tm_abort(tx);
      }
      // now update the finish_cache to remember that at this time, we were
      // still valid
      tx->ts_cache = finish_cache;

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
              tx->turbo = true;
          }
      }
  }

  /**
   *  CTokenTurbo begin:
   */
  void tm_begin(scope_t* scope)
  {
      TX* tx = Self;

      if (++tx->nesting_depth > 1)
          return;

      tx->scope = scope;

      tx->allocator.onTxBegin();

      // get time of last finished txn
      tx->ts_cache = last_complete.val;

      // switch to turbo mode?
      //
      // NB: this only applies to transactions that aborted after doing a write
      if (tx->ts_cache == ((uintptr_t)tx->order - 1))
          tx->turbo = true;
  }

  /**
   *  CTokenTurbo commit (read-only):
   */
  void tm_end()
  {
      TX* tx = Self;
      if (--tx->nesting_depth)
          return;

      if (tx->turbo) {
          CFENCE; // wbw between writeback and last_complete.val update
          last_complete.val = tx->order;

          // set status to committed...
          tx->order = -1;

          // commit all frees, reset all lists
          tx->r_orecs.reset();
          tx->writes.reset();
          tx->allocator.onTxCommit();
          ++tx->commits_rw;
          tx->turbo = false;
          return;
      }

      // NB: we can have no writes but still have an order, if we aborted
      //     after our first write.  In that case, we need to participate in
      //     ordered commit, and can't take the RO fastpath
      if (tx->order == -1) {
          tx->r_orecs.reset();
          tx->allocator.onTxCommit();
          ++tx->commits_ro;
          return;
      }

      // we need to transition to fast here, but not till our turn
      while (last_complete.val != ((uintptr_t)tx->order - 1)) { }

      // validate
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if it has a timestamp of ts_cache or greater, abort
          if (ivt > tx->ts_cache)
              tm_abort(tx);
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
      tx->allocator.onTxCommit();
      ++tx->commits_rw;
  }

  /**
   *  CTokenTurbo read (read-only transaction)
   */
  void* read_ro(TX* tx, void** addr)
  {
      void* tmp = *addr;
      CFENCE; // RBR between dereference and orec check

      // get the orec addr, read the orec's version#
      orec_t* o = get_orec(addr);
      uintptr_t ivt = o->v.all;
      // abort if this changed since the last time I saw someone finish
      if (ivt > tx->ts_cache)
          tm_abort(tx);

      // log orec
      tx->r_orecs.insert(o);

      // possibly validate before returning
      if (last_complete.val > tx->ts_cache) {
          uintptr_t finish_cache = last_complete.val;
          foreach (OrecList, i, tx->r_orecs) {
              // read this orec
              uintptr_t ivt_inner = (*i)->v.all;
              // if it has a timestamp of ts_cache or greater, abort
              if (ivt_inner > tx->ts_cache)
                  tm_abort(tx);
          }
          // now update the ts_cache to remember that at this time, we were
          // still valid
          tx->ts_cache = finish_cache;
      }
      return tmp;
  }

  /**
   *  CTokenTurbo read (writing transaction)
   */
  void* read_rw(TX* tx, void** addr)
  {
      // check the log for a RAW hazard, we expect to miss
      if (tx->writes.size()) {
          // check the log for a RAW hazard, we expect to miss
          WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
          bool found = tx->writes.find(log);
          if (found)
              return log.val;
      }

      void* tmp = *addr;
      CFENCE; // RBR between dereference and orec check

      // get the orec addr, read the orec's version#
      orec_t* o = get_orec(addr);
      uintptr_t ivt = o->v.all;
      // abort if this changed since the last time I saw someone finish
      if (ivt > tx->ts_cache)
          tm_abort(tx);

      // log orec
      tx->r_orecs.insert(o);

      // validate, and if we have writes, then maybe switch to fast mode
      if (last_complete.val > tx->ts_cache)
          validate(tx, last_complete.val);
      return tmp;
  }

  TM_FASTCALL
  void* tm_read(void** addr)
  {
      TX* tx = Self;
      if (tx->turbo) {
          CFENCE;
          return *addr;
      }
      if (tx->order != -1)
          return read_rw(tx, addr);
      return read_ro(tx, addr);
  }


  /**
   *  CTokenTurbo write (read-only context)
   */
  TM_FASTCALL
  void tm_write(void** addr, void* val)
  {
      TX* tx = Self;

      if (tx->turbo) {
          // mark the orec, then update the location
          orec_t* o = get_orec(addr);
          o->v.all = tx->order;
          CFENCE;
          *addr = val;
      }
      else if (tx->order == -1) {
          // we don't have any writes yet, so we need to get an order here
          tx->order = 1 + faiptr(&timestamp.val);

          // record the new value in a redo log
          tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

          // go turbo?
          //
          // NB: we test this on first write, but not subsequent writes, because up
          //     until now we didn't have an order, and thus weren't allowed to use
          //     turbo mode
          validate(tx, last_complete.val);
      }
      else {
          // record the new value in a redo log
          tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      }
  }

  /**
   *  get a chunk of memory that will be automatically reclaimed if the caller
   *  is a transaction that ultimately aborts
   */
  void* tm_alloc(size_t size) { return Self->allocator.txAlloc(size); }

  /**
   *  Free some memory.  If the caller is a transaction that ultimately aborts,
   *  the free will not happen.  If the caller is a transaction that commits,
   *  the free will happen at commit time.
   */
  void tm_free(void* p) { Self->allocator.txFree(p); }

}

/**
 *  Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(CTokenTurbo, ctokenturbo);
REGISTER_TM_FOR_STANDALONE(ctokenturbo, 11);
