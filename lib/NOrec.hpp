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
 *  NOrec Implementation
 *
 *    This STM was published by Dalessandro et al. at PPoPP 2010.  The
 *    algorithm uses a single sequence lock, along with value-based validation,
 *    for concurrency control.  This variant offers semantics at least as
 *    strong as Asymmetric Lock Atomicity (ALA).
 */

#include <stdint.h>
#include <iostream>
#include <cassert>
#include <setjmp.h> // factor this out into the API?
#include "platform.hpp"
#include "ValueList.hpp"
#include "WriteSet.hpp"
#include "WBMMPolicy.hpp"
#include "Macros.hpp"
#include "tx.hpp"
#include "libitm.h"

using namespace stm;

/*** The only metadata we need is a single global padded lock ***/
static pad_word_t timestamp = {0};

static const uintptr_t VALIDATION_FAILED = 1;

/**
 *  Validate a transaction by ensuring that its reads have not changed
 */
static NOINLINE uintptr_t validate(TX* tx)
{
    while (true) {
        // read the lock until it is even
        uintptr_t s = timestamp.val;
        if ((s & 1) == 1)
            continue;

        // check the read set
        CFENCE;
        // don't branch in the loop---consider it backoff if we fail
        // validation early
        bool valid = true;
        foreach (ValueList, i, tx->vlist)
        valid &= STM_LOG_VALUE_IS_VALID(i, tx);

        if (!valid)
            return VALIDATION_FAILED;

        // restart if timestamp changed during read set iteration
        CFENCE;
        if (timestamp.val == s)
            return s;
    }
}

/**
 *  Abort and roll back the transaction (e.g., on conflict).
 */
template <class CM>
static stm::checkpoint_t* rollback(TX* tx)
{
    ++tx->aborts;
    tx->vlist.reset();
    tx->writes.reset();
    tx->allocator.onTxAbort();
    tx->nesting_depth = 0;
    CM::onAbort(tx);
    return &tx->checkpoint;
}

/**
 *  Start a (possibly flat nested) transaction.
 *
 *  [mfs] Eventually need to inline setjmp into this method
 */
template <class CM>
static uint32_t tm_begin(uint32_t) {
    TX* tx = Self;
    if (++tx->nesting_depth == 1) {
        CM::onBegin(tx);

        // Originally, NOrec required us to wait until the timestamp is even
        // before we start.  However, we can round down if odd, in which case
        // we don't need control flow here.

        // Sample the sequence lock, if it is even decrement by 1
        tx->start_time = timestamp.val & ~(1L);

        // notify the allocator
        tx->allocator.onTxBegin();
    }
    return a_runInstrumentedCode | a_saveLiveVariables;
}

/**
 *  Commit a (possibly flat nested) transaction
 */
template <class CM>
static void tm_end()
{
    TX* tx = Self;
    if (--tx->nesting_depth)
        return;

    // read-only is trivially successful at last read
    if (!tx->writes.size()) {
        tx->vlist.reset();
        tx->allocator.onTxCommit();
        ++tx->commits_ro;
        CM::onCommit(tx);
        return;
    }

    // From a valid state, the transaction increments the seqlock.  Then it
    // does writeback and increments the seqlock again

    // get the lock and validate (use RingSTM obstruction-free technique)
    while (!bcasptr(&timestamp.val, tx->start_time, tx->start_time + 1))
        if ((tx->start_time = validate(tx)) == VALIDATION_FAILED)
            tm_abort(tx);

    tx->writes.writeback();

    // Release the sequence lock, then clean up
    CFENCE;
    timestamp.val = tx->start_time + 2;
    CM::onCommit(tx);
    tx->vlist.reset();
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

    // A read is valid iff it occurs during a period where the seqlock does
    // not change and is even.  This code also polls for new changes that
    // might necessitate a validation.

    // read the location to a temp
    void* tmp = *addr;
    CFENCE;

    // if the timestamp has changed since the last read, we must validate and
    // restart this read
    while (tx->start_time != timestamp.val) {
        if ((tx->start_time = validate(tx)) == VALIDATION_FAILED)
            tm_abort(tx);
        tmp = *addr;
        CFENCE;
    }

    // log the address and value, uses the macro to deal with
    // STM_PROTECT_STACK
    STM_LOG_VALUE(tx, addr, tmp, mask);
    return tmp;
}

/**
 *  Simple buffered transactional write
 */
static TM_FASTCALL void tm_write(void** addr, void* val)
{
    TX* tx = Self;
    // just buffer the write
    tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
}
