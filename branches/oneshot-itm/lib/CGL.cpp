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
static const char* tm_getalgname() { return "CGL"; }

/**
 *  Start a transaction: if we're already in a tx, bump the nesting
 *  counter.  Otherwise, grab the lock.  Note that we have a null parameter
 *  so that the signature is identical to all other STMs (prereq for
 *  adaptivity)
 *
 *  This only gets called for the outermost scope.
 */
static uint32_t tm_begin(uint32_t) {
    TX* tx = Self;
    tatas_acquire(&timestamp.val);
    return a_runInstrumentedCode;
}

// /**
//  *  This is a special external function call for the cglapi that bypasses the
//  *  normal _ITM_beginTransaction call that performs a checkpoint.
//  *
//  *  TODO: We'll want a different solution for this in the future.
//  */
// namespace stm {
//   void cgl_tm_begin() {
//       if (++Self->nesting_depth == 1)
//           tm_begin(0x02);
//   }
// }

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
