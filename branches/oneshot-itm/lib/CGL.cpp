/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <stdint.h>
#include <iostream>
#include <cstdlib>
#include "tmabi-weak.hpp"
#include "tx.hpp"
#include "locks.hpp"
#include "metadata.hpp"
#include "adaptivity.hpp"
#include "libitm.h"

using namespace stm;

/**
 * The only metadata we need is a single global padded lock
 */
static pad_word_t timestamp = {0};

/**
 *  For querying to get the current algorithm name
 */
const char* alg_tm_getalgname() {
    return "CGL";
}

/**
 *  This supports CGL in the context of AdaptTM. libCGL uses the weak
 *  definition of _ITM_beginTransaction.
 */
uint32_t alg_tm_begin(uint32_t flags, TX*) {
    assert(flags & pr_hasNoAbort && "CGL does not support cancel");
    tatas_acquire(&timestamp.val);
    return a_runInstrumentedCode;
}

/**
 *  Provide a weak implementation for _ITM_beginTransaction. This will be used
 *  for libCGL, because it will be the only implementation of
 *  _ITM_beginTransaction available.
 *
 *  NB: This requires special build rules for libCGL---we don't want to include
 *      checkpoint-asm.o in the build.
 */
uint32_t ITM_REGPARM __attribute__((weak, returns_twice))
_ITM_beginTransaction(uint32_t flags, ...) {
    if (++Self->nesting_depth > 1)
        return a_runInstrumentedCode;

    return alg_tm_begin(flags, NULL);
}

void alg_tm_end() {
    // End a transaction: decrease the nesting level, then perhaps release the
    // lock and increment the count of commits.
    //
    // NB: we don't know if this is a writer or reader, so we just universally
    //     increment commits_rw.

    TX* tx = Self;
    if (--tx->nesting_depth)
        return;
    tatas_release(&timestamp.val);
    ++tx->commits_rw;
}

void* alg_tm_alloc(size_t s) {
    // Nothing special since CGL is always serial.
    return malloc(s);
}

void* alg_tm_calloc(size_t n, size_t s) {
    // Nothing special since CGL is always serial.
    return calloc(n, s);
}

void alg_tm_free(void* p) {
    // Nothing special since CGL is always serial.
    free(p);
}

void* alg_tm_read(void** addr) {
    return *addr;
}

void alg_tm_write(void** addr, void* val) {
    *addr = val;
}

void alg_tm_rollback(TX*) {
    assert(0 && "Rollback not supported in CGL");
    exit(-1);
}

bool alg_tm_is_irrevocable(TX*) {
    return true;
}

void alg_tm_become_irrevocable(_ITM_transactionState) {
    return;
}

// Register the TM for adaptivity and for use as a standalone library
REGISTER_TM_FOR_ADAPTIVITY(CGL);

/**
 *  Add weak implementations of all of the ITM read and write functions. These
 *  will be used for libCGL.
 */
#define RSTM_LIBITM_READ(SYMBOL, CALLING_CONVENTION, TYPE)              \
    TYPE CALLING_CONVENTION __attribute__((weak))                       \
    SYMBOL(TYPE* addr) {                                                \
        return *addr;                                                   \
    }

#define RSTM_LIBITM_WRITE(SYMBOL, CALLING_CONVENTION, TYPE)             \
    void CALLING_CONVENTION __attribute__((weak))                       \
    SYMBOL(TYPE* addr, TYPE val) {                                      \
        *addr = val;                                                    \
    }

#include "libitm-dtfns.def"

#undef RSTM_LIBITM_WRITE
#undef RSTM_LIBITM_READ
