/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_TM_ALLOC_H
#define RSTM_TM_ALLOC_H

/**
 *  All of the TMs have the same implementation for tm_alloc and tm_free. They
 *  do this because, when they are compiled as standalone libraries, they need
 *  the implementation. We declare them as static to make sure that every
 *  object file includes an implementation.
 */
#include "tx.hpp"

/**
 *  get a chunk of memory that will be automatically reclaimed if the caller
 *  is a transaction that ultimately aborts
 */
static void* alg_tm_alloc(size_t size) {
    return stm::Self->allocator.txAlloc(size);
}

/**
 *  get a chunk of memory that will be automatically reclaimed if the caller
 *  is a transaction that ultimately aborts
 */
static void* alg_tm_calloc(size_t n, size_t s) {
    if (size_t size = n * s)
        stm::Self->allocator.txAlloc(size);
    return NULL;
}

/**
 *  Free some memory.  If the caller is a transaction that ultimately aborts,
 *  the free will not happen.  If the caller is a transaction that commits,
 *  the free will happen at commit time.
 */
static void alg_tm_free(void* p) {
    stm::Self->allocator.txFree(p);
}


#endif // RSTM_TM_ALLOC_H
