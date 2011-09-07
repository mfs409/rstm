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
 *  Note: this is an example that we do not build, but that may be helpful
 *  when debugging and testing.  The example provides a very simple,
 *  lightweight "CGL" stm implementation, without support for nesting or any
 *  other nice features.
 */

#include <stdio.h>
#include <stdlib.h>
#include "alt-license/OracleSkyStuff.hpp"
#include "common/locks.hpp"

/**
 *  Switch for turning on/off debug messages
 */

// #define DEBUG(...) printf(__VA_ARGS__)
#define DEBUG(...)

/**
 *  Every thread needs a descriptor.  We don't actually use one in this fake
 *  STM, so all we do is have a null pointer that we set to non-null when
 *  needed.
 */
__thread void* myDescriptor = NULL;

/**
 *  A single global lock for protecting all transactions
 */
volatile uint32_t LOCK = 0;

namespace stm
{
  /**
   *  The API expects to be able to query the library to find out the name of
   *  the current algorithm.  The value is never used, only printed, so we
   *  can use whatever value suffices to indicate that this is a nonstandard
   *  STM implementation.
   */
  const char* get_algname()
  {
      return "CUSTOM_CGL";
  }
}

extern "C"
{
    /**
     *  The compiler API expects to be able to get a pointer to the
     *  transaction's descriptor.  We don't really have descriptors in this
     *  phony STM, so instead we have this dummy routine.  Note that it
     *  actually returns a pointer to the pointer to the descriptor, because
     *  we're just using the pointer itself as a flag for tracking the first
     *  call to this function.
     */
    void* STM_GetMyTransId()
    {
        void* ret = (void*)&myDescriptor;
        DEBUG("Call to STM_GetMyTransID returning 0x%p\n", ret);
        if (myDescriptor == NULL) {
            getStackInfo();
            myDescriptor = (void*)1;
        }
        return ret;
    }

    /**
     *  Simple begin method: spin until the lock is acquired.  Note that this
     *  does not support nesting.
     */
    BOOL  STM_BeginTransaction(void* theTransId)
    {
        DEBUG("Call to STM_BeginTransaction by 0x%p\n", theTransId);
        while (!bcas32(&LOCK, 0, 1)) { }
        return 1;
    }

    /**
     *  validation has no meaning in our code, because transactions never
     *  abort.  However, it is also the case that in the final shim, this
     *  code will have no meaning, because we will be using setjmp/longjmp to
     *  interface to manage rollback.
     */
    BOOL  STM_ValidateTransaction(void* theTransId)
    {
        DEBUG("Call to STM_ValidateTransaction by 0x%p\n", theTransId);
        return 1;
    }

    /**
     *  For now, commit just means releasing the lock and returning that the
     *  operation succeeded.  However, we will ultimately be using longjmp to
     *  indicate failure.
     */
    CommitStatus  STM_CommitTransaction(void* theTransId)
    {
        DEBUG("Call to STM_CommitTransaction by 0x%p\n", theTransId);
        LOCK = 0;
        return CommittedNoRetry;
    }

    /**
     *  The Oracle API works very hard to separate the acquisition of
     *  locations from the access of those locations.  The mechanism doesn't
     *  apply to postvalidate-only STMs, like RingSTM and NOrec.  For
     *  consistency, we'll make this a no-op, and then do all the work of
     *  acquisition and access from the TranRead function.
     */
    RdHandle* STM_AcquireReadPermission (void* theTransId, void* theAddr, BOOL theValid)
    {
        DEBUG("Call to STM_AcquireReadPermission by 0x%p\n", theTransId);
        return NULL;
    }

    /**
     *  See above: This function has no meaning in our shim.
     */
    WrHandle* STM_AcquireWritePermission (void* theTransId, void* theAddr, BOOL theValid)
    {
        DEBUG("Call to STM_AcquireWritePermission by 0x%p\n", theTransId);
        return NULL;
    }

    /**
     *  Simple read.  In CGL, we just dereference the address and we're good.
     */
    UINT32 STM_TranRead32 (void* theTransId, RdHandle* theRdHandle, UINT32* theAddr, BOOL theValid)
    {
        DEBUG("Call to STM_TranRead32 by 0x%p\n", theTransId);
        return *theAddr;
    }

    /**
     *  Simple write.  In CGL, we just update the address.
     */
    BOOL  STM_TranWrite32 (void* theTransId, WrHandle* theWrHandle, UINT32* theAddr, UINT32 theVal, BOOL theValid)
    {
        DEBUG("Call to STM_TranWrite32 by 0x%p\n", theTransId);
        *theAddr = theVal;
        return true;
    }

    /**
     *  Eventually, this will need to call a transactional malloc function.
     */
    void* STM_TranMalloc(void* theTransId, size_t theSize)
    {
        return malloc(theSize);
    }

    /**
     *  Eventually, this will need to call a transactional free function.
     */
    void  STM_TranMFree(void* theTransId, void *theMemBlock)
    {
        free(theMemBlock);
    }

} // extern "C"
