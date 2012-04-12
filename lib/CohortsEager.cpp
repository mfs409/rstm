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
 *  CohortsEager Implementation
 *
 *  Similiar to Cohorts, except that if I'm the last one in the cohort, I
 *  go to turbo mode, do in place read and write, and do turbo commit.
 */

#include <stdint.h>
#include <iostream>
#include <cassert>
#include "byte-logging.hpp"
#include "tmabi-weak.hpp"               // the weak interface
#include "foreach.hpp"                  // FOREACH macro
#include "inst.hpp"
#include "WriteSet.hpp"
#include "WBMMPolicy.hpp" // todo: remove this, use something simpler
#include "tx.hpp"
#include "adaptivity.hpp"
#include "tm_alloc.hpp"
#include "libitm.h"

// define atomic operations
#define CAS __sync_val_compare_and_swap
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch

using namespace stm;

// Global variables for Cohorts
static volatile uint32_t locks[9] = {0}; // a big lock at locks[0], and
                                         // small locks from locks[1] to
                                         // locks[8]
static volatile int32_t started = 0;     // number of tx started
static volatile int32_t cpending = 0;    // number of tx waiting to commit
static volatile int32_t committed = 0;   // number of tx committed
static volatile int32_t last_order = 0;  // order of last tx in a cohort + 1
static volatile uint32_t gatekeeper = 0; // indicating whether tx can start
static volatile uint32_t inplace = 0;

static pad_word_t last_complete = {0};

/**
 *  This is the Orec Timestamp, the NOrec/TML seqlock, the CGL lock, and the
 *  RingSW ring index
 */
static pad_word_t timestamp = {0};

/**
 *  For querying to get the current algorithm name
 */
const char* alg_tm_getalgname() {
    return "CohortsEager";
}

/**
 *  Abort and roll back the transaction (e.g., on conflict).
 */
void alg_tm_rollback(TX* tx) {
    ++tx->aborts;
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->allocator.onTxAbort();
}

/**
 *  Validate a transaction by ensuring that its reads have not changed
 */
static NOINLINE void validate(TX* tx) {
    FOREACH (OrecList, i, tx->r_orecs) {
        // read this orec
        uintptr_t ivt = (*i)->v.all;
        // If orec changed , abort
        //
        // [mfs] norec recently switched to full validation, with a return
        //       val of true or false depending on whether or not to abort.
        //       Should evaluate if that is faster here.
        if (ivt > tx->ts_cache) {
            // increase total number of committed tx
            // ADD(&committed, 1);
            committed++;
            WBR;
            // set self as completed
            last_complete.val = tx->order;
            // abort
            _ITM_abortTransaction(TMConflict);
        }
    }
}

/**
 * only called for outermost transactions.
 */
uint32_t alg_tm_begin(uint32_t, TX* tx) {
  S1:
    // wait until everyone is committed
    while (cpending != committed);

    // before tx begins, increase total number of tx
    ADD(&started, 1);

    // [NB] we must double check no one is ready to commit yet
    // and no one entered in place write phase(turbo mode)
    if (cpending > committed || inplace == 1) {
        SUB(&started, 1);
        goto S1;
    }

    tx->allocator.onTxBegin();
    // get time of last finished txn
    tx->ts_cache = last_complete.val;

    return a_runInstrumentedCode;
}

/**
 *  Commit a (possibly flat nested) transaction
 */
void alg_tm_end() {
    TX* tx = Self;
    if (--tx->nesting_depth)
        return;

    if (tx->turbo) {
        // increase # of tx waiting to commit, and use it as the order
        cpending++;

        // clean up
        tx->r_orecs.reset();
        tx->allocator.onTxCommit();
        ++tx->commits_rw;

        // wait for my turn, in this case, cpending is my order
        while (last_complete.val != (uintptr_t)(cpending - 1));

        // reset in place write flag
        inplace = 0;

        // mark self as done
        last_complete.val = cpending;

        // increase # of committed
        committed ++;
        WBR;
        tx->turbo = false;
        return;
    }

    if (!tx->writes.size()) {
        // decrease total number of tx started
        SUB(&started, 1);

        // clean up
        tx->r_orecs.reset();
        tx->allocator.onTxCommit();
        ++tx->commits_ro;
        return;
    }

    // increase # of tx waiting to commit, and use it as the order
    tx->order = ADD(&cpending ,1);

    // Wait for my turn
    while (last_complete.val != (uintptr_t)(tx->order - 1));

    // Wait until all tx are ready to commit
    while (cpending < started);

    // If in place write occurred, all tx validate reads
    // Otherwise, only first one skips validation
    if (inplace == 1 || tx->order != last_order)
        validate(tx);

    FOREACH (WriteSet, i, tx->writes) {
        // get orec
        orec_t* o = get_orec(i->addr);
        // mark orec
        o->v.all = tx->order;
        // do write back
        *i->addr = i->val;
    }

    // increase total number of committed tx
    // [NB] Using atomic instruction might be faster
    // ADD(&committed, 1);
    committed ++;
    WBR;

    // update last_order
    last_order = started + 1;

    // mark self as done
    last_complete.val = tx->order;

    // commit all frees, reset all lists
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->allocator.onTxCommit();
    ++tx->commits_rw;
}

/**
 *  Transactional read
 */
static inline void* alg_tm_read_aligned_word(void** addr, TX* tx) {
    // log orec
    tx->r_orecs.insert(get_orec(addr));
    return *addr;
}

/**
 *  Simple buffered transactional write
 */
static inline void ALG_TM_WRITE_WORD(void** addr, void* val, TX* tx, uintptr_t mask) {
    if (tx->turbo) {
        orec_t* o = get_orec(addr);
        o->v.all = started; // mark orec
        *addr = val; // in place write
        return;
    }

    if (!tx->writes.size() && 0) {
        // If everyone else is ready to commit, do in place write
        if (cpending + 1 == started) {
            // set up flag indicating in place write starts
            // [NB] When testing on MacOS, better use CAS
            inplace = 1;
            WBR;
            // double check is necessary
            if (cpending + 1 == started) {
                // mark orec
                orec_t* o = get_orec(addr);
                o->v.all = started;
                // in place write
                *addr = val;
                // go turbo mode
                tx->turbo = true;
                return;
            }
            // reset flag
            inplace = 0;
        }
        tx->writes.insert(WriteSetEntry(REDO_LOG_ENTRY(addr, val, mask)));
        return;
    }

    // record the new value in a redo log
    tx->writes.insert(WriteSetEntry(REDO_LOG_ENTRY(addr, val, mask)));
}

void* alg_tm_read(void** addr) {
        return inst::read<void*,
                          inst::TurboFilter<inst::NoFilter>, // turbo filter
                          inst::WordlogRAW, // log at the word granularity
                          inst::NoReadOnly, // no separate read-only code
                          true              // force align all accesses
                          >(addr);
}

void alg_tm_write(void** addr, void* val) {
    ALG_TM_WRITE_WORD(addr, val, Self, ~0);
}

bool alg_tm_is_irrevocable(TX* tx) {
    assert(false && "Uninstrumented!");
    return false;
}

void alg_tm_become_irrevocable(_ITM_transactionState) {
    assert(false && "Unimplemented");
    return;
}

/**
 *  Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(CohortsEager)

