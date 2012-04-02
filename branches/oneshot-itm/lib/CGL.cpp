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
static const char* tm_getalgname() {
    return "CGL";
}

/**
 *  This supports CGL in the context of AdaptTM. libCGL uses the weak
 *  definition of _ITM_beginTransaction.
 */
static uint32_t TM_FASTCALL tm_begin(uint32_t flags, TX*) {
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
uint32_t __attribute__((weak)) _ITM_beginTransaction(uint32_t flags, ...) {
    assert(flags & pr_hasNoAbort && "CGL does not support cancel");
    tatas_acquire(&timestamp.val);
    return a_runInstrumentedCode;
}

/**
 *  End a transaction: decrease the nesting level, then perhaps release the
 *  lock and increment the count of commits.
 */
static void tm_end() {
    TX* tx = Self;
    if (--tx->nesting_depth)
        return;
    tatas_release(&timestamp.val);
    ++tx->commits_rw;
}

/**
 *  In CGL, malloc doesn't need any special care
 */
static void* tm_alloc(size_t s) {
    return malloc(s);
}

/**
 *  In CGL, free doesn't need any special care
 */
static void tm_free(void* p) {
    free(p);
}

/**
 *  CGL read
 */
static void* TM_FASTCALL tm_read(void** addr) {
    return *addr;
}

/**
 *  CGL write
 */
static void TM_FASTCALL tm_write(void** addr, void* val) {
    *addr = val;
}

static checkpoint_t* rollback(TX* tx) {
    assert(0 && "Rollback not supported in CGL");
    exit(-1);
    return NULL;
}

/**
 *  Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(CGL);
REGISTER_TM_FOR_STANDALONE();
