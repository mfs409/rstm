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
#include "byte-logging.hpp"
#include "tmabi-weak.hpp"               // the weak abi declarations
#include "foreach.hpp"                  // the FOREACH macro
#include "ValueList.hpp"
#include "WBMMPolicy.hpp"
#include "tx.hpp"
#include "libitm.h"
#include "inst.hpp"                     // the generic R/W instrumentation

using stm::TX;
using stm::pad_word_t;
using stm::WriteSet;
using stm::ValueList;
using stm::ValueListEntry;
using stm::Self;

/*** The only metadata we need is a single global padded lock ***/
static pad_word_t timestamp = {0};

static const uintptr_t VALIDATION_FAILED = 1;

/**
 *  Validate a transaction by ensuring that its reads have not changed
 */
static NOINLINE uintptr_t validate(TX* tx) {
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
        FOREACH (ValueList, i, tx->vlist) {
            valid &= STM_LOG_VALUE_IS_VALID(i, tx);
        }

        if (!valid)
            return VALIDATION_FAILED;

        // restart if timestamp changed during read set iteration
        CFENCE;
        if (timestamp.val == s)
            return s;
    }
}

/** Abort and roll back the transaction (e.g., on conflict). */
template <class CM>
static void alg_tm_rollback(TX* tx) {
    ++tx->aborts;
    tx->vlist.reset();
    tx->writes.clear();
    tx->allocator.onTxAbort();
    CM::onAbort(tx);
}

/**
 *  Begin an outermost transactions, nested begins are absorbed in the
 *  _ITM_beginTransaction asm.
 */
template <class CM>
static uint32_t TM_FASTCALL alg_tm_begin(uint32_t, TX* tx) {
    CM::onBegin(tx);

    // Originally, NOrec required us to wait until the timestamp is even
    // before we start.  However, we can round down if odd, in which case
    // we don't need control flow here.

    // Sample the sequence lock, if it is even decrement by 1
    tx->start_time = timestamp.val & ~(1L);

    // notify the allocator
    tx->allocator.onTxBegin();

    return a_runInstrumentedCode;
}

/** Commit a (possibly flat nested) transaction. */
template <class CM>
static void alg_tm_end() {
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
            _ITM_abortTransaction(TMConflict);

    tx->writes.redo();

    // Release the sequence lock, then clean up
    CFENCE;
    timestamp.val = tx->start_time + 2;
    CM::onCommit(tx);
    tx->vlist.reset();
    tx->writes.clear();
    tx->allocator.onTxCommit();
    ++tx->commits_rw;
}

/**
 *  The essence of the NOrec read algorithm, for use with the inst.hpp
 *  infrastructure.
 */
static inline void* alg_tm_read_aligned_word(void** addr, TX* tx, uintptr_t mask) {
    // read the location to a temp
    void* tmp = *addr;
    CFENCE;

    // if the timestamp has changed since the last read, we must validate and
    // restart this read
    while (tx->start_time != timestamp.val) {
        if ((tx->start_time = validate(tx)) == VALIDATION_FAILED)
            _ITM_abortTransaction(TMConflict);
        tmp = *addr;
        CFENCE;
    }

    // tmp contains the value we want
    tx->vlist.insert(addr, tmp, mask);
    return tmp;
}

static inline void* alg_tm_read_aligned_word_ro(void** addr, TX* tx,
                                                uintptr_t mask)
{
    return alg_tm_read_aligned_word(addr, tx, mask);
}

/** Simple buffered transactional write. */
static inline void alg_tm_write_aligned_word(void** addr, void* val, TX* tx, uintptr_t mask) {
    // just buffer the write
    tx->writes.insert(addr, WriteSet::Word(val, mask));
}

/** The library api interface to read an aligned word. */
void* alg_tm_read(void** addr) {
    return stm::inst::read<void*,                 //
                           stm::inst::NoFilter,   // don't pre-filter accesses
                           stm::inst::WordlogRAW, // log at the word granularity
                           stm::inst::NoReadOnly, // no separate read-only code
                           true                   // force align all accesses
                           >(addr);
}

/** The library api interface to write an aligned word. */
void alg_tm_write(void** addr, void* val) {
    stm::inst::write<void*, stm::inst::NoFilter, true>(addr, val);
}

bool alg_tm_is_irrevocable(TX*) {
    assert(false && "Unimplemented");
    return false;
}

void alg_tm_become_irrevocable(_ITM_transactionState) {
    assert(false && "Unimplemented");
    return;
}

/**
 *  Instantiate our read template for all of the read types, and add weak
 *  aliases for the LIBITM symbols to them.
 *
 *  TODO: We can't make weak aliases without mangling the symbol names, but
 *        this is non-trivial for the instrumentation templates. For now, we
 *        just inline the read templates into weak versions of the library. We
 *        could use gcc's asm() exetension to instantiate the template with a
 *        reasonable name...?
 */
#define RSTM_LIBITM_READ(SYMBOL, CALLING_CONVENTION, TYPE)              \
    TYPE CALLING_CONVENTION __attribute__((weak)) SYMBOL(TYPE* addr) {  \
        return stm::inst::read<TYPE,                                    \
                               stm::inst::FullFilter,                   \
                               stm::inst::WordlogRAW,                   \
                               stm::inst::NoReadOnly,                   \
                               false>(addr);                            \
    }

#define RSTM_LIBITM_WRITE(SYMBOL, CALLING_CONVENTION, TYPE)             \
    void CALLING_CONVENTION __attribute__((weak))                       \
    SYMBOL(TYPE* addr, TYPE val) {                                      \
        stm::inst::write<TYPE,                                          \
                         stm::inst::FullFilter,                         \
                         false>(addr, val);                             \
    }

#include "libitm-dtfns.def"

#undef RSTM_LIBITM_WRITE
#undef RSTM_LIBITM_READ
