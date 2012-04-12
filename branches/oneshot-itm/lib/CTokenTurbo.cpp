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
#include <unistd.h>
#include "byte-logging.hpp"
#include "inst.hpp"
#include "tmabi-weak.hpp"
#include "foreach.hpp"
#include "WriteSet.hpp"
#include "WBMMPolicy.hpp"
#include "tx.hpp"
#include "adaptivity.hpp"
#include "tm_alloc.hpp"
#include "libitm.h"

using namespace stm;

/*** The only metadata we need is a single global padded lock ***/
static pad_word_t timestamp = {0};
static pad_word_t last_complete = {0};

/**
 *  For querying to get the current algorithm name
 */
const char* alg_tm_getalgname() {
    return "CTokenTurbo";
}

/**
 *  CTokenTurbo unwinder:
 *
 *    NB: self-aborts in Turbo Mode are not supported.  We could add undo
 *        logging to address this, and add it in Pipeline too.
 */
void alg_tm_rollback(TX* tx)
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
}

/**
 *  CTokenTurbo validation
 */
static NOINLINE void validate(TX* tx, uintptr_t finish_cache)
{
    FOREACH (OrecList, i, tx->r_orecs) {
        // read this orec
        uintptr_t ivt = (*i)->v.all;
        // if it has a timestamp of ts_cache or greater, abort
        if (ivt > tx->ts_cache)
            _ITM_abortTransaction(TMConflict);
    }
    // now update the finish_cache to remember that at this time, we were
    // still valid
    tx->ts_cache = finish_cache;

    // and if we are now the oldest thread, transition to fast mode
    if (tx->ts_cache == ((uintptr_t)tx->order - 1)) {
        if (tx->writes.size() != 0) {
            // mark every location in the write set, and perform write-back
            FOREACH (WriteSet, i, tx->writes) {
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
 *  CTokenTurbo begin: only called for outermost transactions.
 */
uint32_t alg_tm_begin(uint32_t, TX* tx)
{
    tx->allocator.onTxBegin();

    // get time of last finished txn
    tx->ts_cache = last_complete.val;

    // switch to turbo mode?
    //
    // NB: this only applies to transactions that aborted after doing a write
    if (tx->ts_cache == ((uintptr_t)tx->order - 1))
        tx->turbo = true;

    return a_runInstrumentedCode;
}

/**
 *  CTokenTurbo commit (read-only):
 */
void alg_tm_end()
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
    FOREACH (OrecList, i, tx->r_orecs) {
        // read this orec
        uintptr_t ivt = (*i)->v.all;
        // if it has a timestamp of ts_cache or greater, abort
        if (ivt > tx->ts_cache)
            _ITM_abortTransaction(TMConflict);
    }
    // writeback
    if (tx->writes.size() != 0) {
        // mark every location in the write set, and perform write-back
        FOREACH (WriteSet, i, tx->writes) {
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
static inline void* alg_tm_read_aligned_word_ro(void** addr, TX* tx, uintptr_t) {
    void* tmp = *addr;
    CFENCE; // RBR between dereference and orec check

    // get the orec addr, read the orec's version#
    orec_t* o = get_orec(addr);
    uintptr_t ivt = o->v.all;
    // abort if this changed since the last time I saw someone finish
    if (ivt > tx->ts_cache)
        _ITM_abortTransaction(TMConflict);

    // log orec
    tx->r_orecs.insert(o);

    // possibly validate before returning
    if (last_complete.val > tx->ts_cache) {
        uintptr_t finish_cache = last_complete.val;
        FOREACH (OrecList, i, tx->r_orecs) {
            // read this orec
            uintptr_t ivt_inner = (*i)->v.all;
            // if it has a timestamp of ts_cache or greater, abort
            if (ivt_inner > tx->ts_cache)
                _ITM_abortTransaction(TMConflict);
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
static inline void* alg_tm_read_aligned_word(void** addr, TX* tx, uintptr_t) {
    void* tmp = *addr;
    CFENCE; // RBR between dereference and orec check

    // get the orec addr, read the orec's version#
    orec_t* o = get_orec(addr);
    uintptr_t ivt = o->v.all;
    // abort if this changed since the last time I saw someone finish
    if (ivt > tx->ts_cache)
        _ITM_abortTransaction(TMConflict);

    // log orec
    tx->r_orecs.insert(o);

    // validate, and if we have writes, then maybe switch to fast mode
    if (last_complete.val > tx->ts_cache)
        validate(tx, last_complete.val);
    return tmp;
}

/**
 *  CTokenTurbo write (read-only context)
 */
static inline void alg_tm_write_aligned_word(void** addr, void* val, TX* tx, uintptr_t mask)
{
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
        tx->writes.insert(addr, val, mask);

        // go turbo?
        //
        // NB: we test this on first write, but not subsequent writes, because up
        //     until now we didn't have an order, and thus weren't allowed to use
        //     turbo mode
        validate(tx, last_complete.val);
    }
    else {
        // record the new value in a redo log
        tx->writes.insert(addr, val, mask);
    }
}

namespace {
  struct CTokenTurboReadOnly {
      static inline bool IsReadOnly(TX* tx) {
          return (tx->order == -1);
      }
  };
}

void* alg_tm_read(void** addr) {
    return inst::read<void*,
                      inst::TurboFilter<inst::NoFilter>, // turbo filter
                      inst::WordlogRAW,    // log at the word granularity
                      CTokenTurboReadOnly, // check's tx order
                      true                 // force align all accesses
                      >(addr);
}

void alg_tm_write(void** addr, void* val) {
    alg_tm_write_aligned_word(addr, val, Self, ~0);
}

bool alg_tm_is_irrevocable(TX* tx) {
    return (tx->turbo);
}

void alg_tm_become_irrevocable(_ITM_transactionState) {
    assert(false && "Unimplemented");
    return;
}

/**
 *  Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(CTokenTurbo)

