/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef LIBOTM2STM_OTM_INLINER_I__
#define LIBOTM2STM_OTM_INLINER_I__

// when including the RSTM inline header, don't include the parts that have
// actual code.  Only include the parts with declarations
#define RSTM_INLINE_HEADER
#include "alt-license/OracleSkyStuff.hpp"

extern "C"
{
    /**
     *  The Oracle API works very hard to separate the acquisition of
     *  locations from the access of those locations.  Since we want RSTM to
     *  be fully general to any/all compilers, we make these mechanisms do
     *  nothing, and keep the acquisition logic in the same place as the
     *  access logic.
     */
    RdHandle* STM_AcquireReadPermission (void*, void*, BOOL) { return NULL; }

    /**
     *  See above: This function has no meaning in our shim.
     */
    WrHandle* STM_AcquireWritePermission(void*, void*, BOOL) { return NULL; }

    /**
     *  See above: This function has no meaning in our shim.
     */
    WrHandle* STM_AcquireReadWritePermission(void*, void*, BOOL) { return NULL; }

    /**
     *  In OracleTM, a read or write may detect a conflict and mark a
     *  transaction as dead, but the method will return, and it will be the
     *  responsibility of the compiler instrumentation to call this function
     *  to see if the stack needs to be unwound and the transaction undone.
     *  In our shim, we use setjmp/longjmp, so this method is unnecessary.
     *  We just return 1 for now.
     */
    BOOL STM_ValidateTransaction(void* theTransId) { return 1; }
}

#endif // LIBOTM2STM_OTM_INLINER_I__
