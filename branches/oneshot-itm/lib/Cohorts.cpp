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
 *  Cohorts Implementation
 *
 *  Original cohorts algorithm
 */

#include <stdint.h>
#include <iostream>
#include <cassert>
#include <setjmp.h> // factor this out into the API?
#include "platform.hpp"
#include "WBMMPolicy.hpp" // todo: remove this, use something simpler
#include "Macros.hpp"
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
static volatile uint32_t locks[9] = {0};  // a big lock at locks[0], and
// small locks from locks[1] to
// locks[8]
static volatile int32_t started = 0;    // number of tx started
static volatile int32_t cpending = 0;   // number of tx waiting to commit
static volatile int32_t committed = 0;  // number of tx committed
static volatile int32_t last_order = 0; // order of last tx in a cohort + 1
static volatile uint32_t gatekeeper = 0;// indicating whether tx can start

static pad_word_t last_complete = {0};

/**
 *  This is the Orec Timestamp, the NOrec/TML seqlock, the CGL lock, and the
 *  RingSW ring index
 */
static pad_word_t timestamp = {0};

/**
 *  For querying to get the current algorithm name
 */
static const char* tm_getalgname() {
    return "Cohorts";
}

/**
 *  Abort and roll back the transaction (e.g., on conflict).
 */
static checkpoint_t* rollback(TX* tx) {
    ++tx->aborts;
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->allocator.onTxAbort();
    return &tx->checkpoint;
}

/**
 *  Validate a transaction by ensuring that its reads have not changed
 */
static NOINLINE void validate(TX* tx) {
    foreach (OrecList, i, tx->r_orecs) {
        // read this orec
        uintptr_t ivt = (*i)->v.all;
        // If orec changed , abort
        //
        // [mfs] norec recently switched to full validation, with a return
        //       val of true or false depending on whether or not to abort.
        //       Should evaluate if that is faster here.
        if (ivt > tx->ts_cache) {
            // increase total number of committed tx
            ADD(&committed, 1);
            // set self as completed
            last_complete.val = tx->order;
            // abort
            tm_abort(tx);
        }
    }
}

/**
 *  only called for outermost transactions.
 */
static uint32_t tm_begin(uint32_t, TX* tx) {
  S1:
    // wait until everyone is committed
    while (cpending != committed);

    // before tx begins, increase total number of tx
    ADD(&started, 1);

    // [NB] we must double check no one is ready to commit yet
    // and no one entered in place write phase(turbo mode)
    if (cpending > committed) {
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
static void tm_end()
{
    TX* tx = Self;
    if (--tx->nesting_depth)
        return;

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

    // If I'm not the first one in a cohort to commit, validate read
    if (tx->order != last_order)
        validate(tx);

    foreach (WriteSet, i, tx->writes) {
        // get orec
        orec_t* o = get_orec(i->addr);
        // mark orec
        o->v.all = tx->order;
    }

    // Wait until all tx are ready to commit
    while (cpending < started);

    // do write back
    foreach (WriteSet, i, tx->writes)
    *i->addr = i->val;

    // update last_order
    last_order = started + 1;

    // mark self as done
    last_complete.val = tx->order;

    // increase total number of committed tx
    // [NB] atomic increment is faster here
    ADD(&committed, 1);
    // committed++;
    // WBR;

    // commit all frees, reset all lists
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->allocator.onTxCommit();
    ++tx->commits_rw;
}

/**
 *  Transactional read
 */
static TM_FASTCALL void* tm_read(void** addr)
{
    TX* tx = Self;

    if (tx->writes.size()) {
        // check the log for a RAW hazard, we expect to miss
        WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
        bool found = tx->writes.find(log);
        if (found)
            return log.val;
    }

    // log orec
    tx->r_orecs.insert(get_orec(addr));
    return *addr;
}

/**
 *  Simple buffered transactional write
 */
static TM_FASTCALL void tm_write(void** addr, void* val)
{
    TX* tx = Self;

    // record the new value in a redo log
    tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
}

/**
 *  Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(Cohorts)
REGISTER_TM_FOR_STANDALONE()
