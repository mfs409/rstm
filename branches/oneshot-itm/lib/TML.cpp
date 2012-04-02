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
 *  TML Implementation
 *
 *    This STM was published by Dalessandro et al. at EuroPar 2010.  The
 *    algorithm allows multiple readers or a single irrevocable writer.  The
 *    semantics are at least as strong as ALA.
 *
 *    NB: now that we dropped the inlined-tml instrumentation hack, we should
 *        probably add ro/rw functions
 */

#include <cassert>
#include "tx.hpp"
#include "platform.hpp"
#include "adaptivity.hpp"
#include "tm_alloc.hpp"
#include "libitm.h"

using namespace stm;

/*** The only metadata we need is a single global padded lock ***/
static pad_word_t timestamp = {0};

/**
 *  For querying to get the current algorithm name
 */
static const char* tm_getalgname() {
    return "TML";
}

/**
 *  Abort and roll back the transaction (e.g., on conflict).
 */
static checkpoint_t* rollback(TX* tx) {
    ++tx->aborts;
    tx->allocator.onTxAbort();
    return &tx->checkpoint;
}

/**
 *  TML requires this to be called after every read
 */
inline static void afterread_TML(TX* tx) {
    CFENCE;
    if (__builtin_expect(timestamp.val != tx->start_time, false))
        tm_abort(tx);
}

/**
 *  TML requires this to be called before every write
 */
inline static void beforewrite_TML(TX* tx) {
    // acquire the lock, abort on failure
    if (!bcasptr(&timestamp.val, tx->start_time, tx->start_time + 1))
        tm_abort(tx);
    ++tx->start_time;
    tx->turbo = true;
}

/**
 *  Start a (possibly flat nested) transaction. Only called for outer
 *  transactions.
 */
static uint32_t TM_FASTCALL tm_begin(uint32_t, TX* tx) {
    // Sample the sequence lock until it is even (unheld)
    //
    // [mfs] Consider using NOrec trick to just decrease and start
    // running... we'll die more often, but with less overhead for readers...
    while ((tx->start_time = timestamp.val) & 1) { }

    // notify the allocator
    tx->allocator.onTxBegin();
    return a_runInstrumentedCode;
}

/**
 *  Commit a (possibly flat nested) transaction
 */
static void tm_end() {
    TX* tx = Self;
    if (--tx->nesting_depth)
        return;

    // writing context: release lock, free memory, remember commit
    if (tx->turbo) {
        ++timestamp.val;
        tx->turbo = false;
        tx->allocator.onTxCommit();
        ++tx->commits_rw;
    }
    // reading context: just remember the commit
    else {
        tx->allocator.onTxCommit();
        ++tx->commits_ro;
    }
}

/**
 *  Transactional read
 */
static TM_FASTCALL void* tm_read(void** addr) {
    TX* tx = Self;
    void* val = *addr;
    if (tx->turbo)
        return val;
    // NB:  afterread_tml includes a CFENCE
    afterread_TML(tx);
    return val;
}

/**
 *  Simple buffered transactional write
 */
static TM_FASTCALL void tm_write(void** addr, void* val) {
    TX* tx = Self;
    if (tx->turbo) {
        *addr = val;
        return;
    }
    // NB:  beforewrite_tml includes a fence via CAS
    beforewrite_TML(tx);
    *addr = val;
}

/**
 *  Register the TM for adaptivity
 */
REGISTER_TM_FOR_ADAPTIVITY(TML)
REGISTER_TM_FOR_STANDALONE()
