/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "libitm.h"
#include "stm/txthread.hpp"
#include "Transaction.h"

#ifdef __cplusplus
extern "C" {
#endif

// ??? _ITM_getTransaction is not inlined, improvement with inlining?
// ??? suggest to Martin to add, low_addr and high_addr (or size) as paramter of _ITM_beginTransaction
// ??? ask Martin, but since we are not saving the stack here, no need for noinline
// __attribute__ ((noinline))
void tanger_stm_save_restore_stack(void* low_addr, void* high_addr)
{
    _ITM_getTransaction()->recordStackInfo(low_addr, high_addr);
    // TM-runtime cannot save stack here because LLVM can add code between this call and _ITM_beginTransaction. It is thus done in _ITM_beginTransaction.
}

typedef void tanger_stm_tx_t;

tanger_stm_tx_t *tanger_stm_get_tx()
{
    return (tanger_stm_tx_t *)_ITM_getTransaction();
}

/* C Memory allocation part */

void *_ITM_malloc (size_t sz)
{
    return stm::Self->allocator.txAlloc(sz);
}

void *_ITM_calloc (size_t sz, size_t nb)
{
    void* ptr = stm::Self->allocator.txAlloc(sz);
    memset(ptr, 0, sz*nb);
    return ptr;
}

void _ITM_free (void * ptr)
{
    stm::Self->allocator.txFree(ptr);
}

#ifdef __cplusplus
}
#endif

