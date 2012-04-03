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
#include "platform.hpp"
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
    assert(flags & pr_hasNoAbort && "CGL does not support cancel");
    if (++Self->nesting_depth > 1)
        return a_runInstrumentedCode;

    tatas_acquire(&timestamp.val);
    return a_runInstrumentedCode;
}

/**
 *  End a transaction: decrease the nesting level, then perhaps release the
 *  lock and increment the count of commits.
 *
 *  NB: we don't know if this is a writer or reader, so we just universally
 *      increment commits_rw.
 */
void alg_tm_end() {
    TX* tx = Self;
    if (--tx->nesting_depth)
        return;
    tatas_release(&timestamp.val);
    ++tx->commits_rw;
}

/**
 *  In CGL, malloc doesn't need any special care
 */
void* alg_tm_alloc(size_t s) {
    return malloc(s);
}

/**
 *  In CGL, free doesn't need any special care
 */
void alg_tm_free(void* p) {
    free(p);
}

/**
 *  CGL read
 */
void* alg_tm_read(void** addr) {
    return *addr;
}

/**
 *  CGL write
 */
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

/**
 *  Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(CGL);
