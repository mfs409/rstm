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
#include <setjmp.h>
#include <common/platform.hpp>
#include <stm/txthread.hpp>
#include "alt-license/OracleSkyStuff.hpp"
#include "common/locks.hpp"
#include "stm/metadata.hpp"

/**
 *  Switch for turning on/off debug messages
 */

//#define DEBUG(...) printf(__VA_ARGS__)
#define DEBUG(...)

namespace stm
{
  void sys_init(void (*abort_handler)(TxThread*) = NULL);
}

/**
 *  In OTM, the compiler adds instrumentation to manually unwinde the
 *  transaction, one stack frame at a time.  This makes sense (especially
 *  on SPARC), but it's bad for RSTM, because RSTM assumes setjmp/longjmp
 *  unwinding (or its equivalent).  We don't want to rewrite all our
 *  algorithms to support dual mechanisms for unwind, so instead we use a
 *  macro and this code at begin time.
 *
 *  The macro (TM_BEGIN, in cxxtm.hpp) performs a setjmp, calls this, and
 *  then invokes the __transaction construct.  In this way, we checkpoint
 *  the current stack, and then before actually starting the transaction,
 *  this code will determine if the jump buffer needs to be saved (and
 *  write-read ordering enforced), and if so, it will do that work, which
 *  is essentially half of the begin method from library.hpp
 */
void OTM_PREBEGIN(stm::scope_t* s)
{
    // get the descriptor
    stm::TxThread* tx = (stm::TxThread*)stm::Self;
    if (!tx) {
        stm::sys_init(NULL);
        stm::TxThread::thread_init();
        tx = (stm::TxThread*)stm::Self;
    }
    // if we are already in a transaction, just return
    if (++tx->nesting_depth > 1)
        return;

    // we must ensure that the write of the transaction's scope occurs
    // *before* the read of the begin function pointer.  On modern x86, a
    // CAS is faster than using WBR or xchg to achieve the ordering.  On
    // SPARC, WBR is best.
#ifdef STM_CPU_SPARC
    tx->scope = s; WBR;
#else
    // NB: this CAS fails on a transaction restart... is that too expensive?
    casptr((volatile uintptr_t*)&tx->scope, (uintptr_t)0, (uintptr_t)s);
#endif
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
        return (stm::TxThread*)stm::Self;
    }

    /**
     *  Simple begin method: spin until the lock is acquired.  Note that this
     *  does not support nesting.
     *
     *  [mfs] comment bitrot warning
     */
    BOOL STM_BeginTransaction(void* theTransId)
    {
        DEBUG("Call to STM_BeginTransaction by 0x%p\n", theTransId);
        stm::TxThread* tx = (stm::TxThread*)theTransId;

        // copied from library.hpp.  Be careful about bit-rot

        // some adaptivity mechanisms need to know nontransactional and
        // transactional time.  This code suffices, because it gets the time
        // between transactions.  If we need the time for a single transaction,
        // we can run ProfileTM
        if (tx->end_txn_time)
            tx->total_nontxn_time += (tick() - tx->end_txn_time);

        // now call the per-algorithm begin function
        stm::TxThread::tmbegin(tx);
        return 1;
    }

    /**
     *  validation has no meaning in our code, because transactions never
     *  abort.  However, it is also the case that in the final shim, this
     *  code will have no meaning, because we will be using setjmp/longjmp to
     *  interface to manage rollback.
     */
    BOOL STM_ValidateTransaction(void* theTransId)
    {
        DEBUG("Call to STM_ValidateTransaction by 0x%p\n", theTransId);
        return 1;
    }

    /**
     *  For now, commit just means releasing the lock and returning that the
     *  operation succeeded.  However, we will ultimately be using longjmp to
     *  indicate failure.
     *
     *  [mfs] beware bitrot relative to library.hpp.  Note bitrot in comment
     *        above
     */
    CommitStatus STM_CommitTransaction(void* theTransId)
    {
        stm::TxThread* tx = (stm::TxThread*) theTransId;
        // [mfs] I don't know how the SunCC nesting interface works.  It's
        //       possible that we should be returning something other than
        //       CommittedNoRetry, but we won't worry about it for now.
        if (--tx->nesting_depth)
            return CommittedNoRetry;
        tx->tmcommit(tx);
        CFENCE;
        tx->scope = NULL;
        tx->end_txn_time = tick();


        DEBUG("Call to STM_CommitTransaction by 0x%p\n", theTransId);
        return CommittedNoRetry;
    }

    /**
     *  The Oracle API works very hard to separate the acquisition of
     *  locations from the access of those locations.  Since we want RSTM to
     *  be fully general to any/all compilers, we make these mechanisms do
     *  nothing, and keep the acquisition logic in the same place as the
     *  access logic.
     */
    RdHandle* STM_AcquireReadPermission (void*, void*, BOOL)
    {
        return NULL;
    }

    /***  See above: This function has no meaning in our shim. */
    WrHandle* STM_AcquireWritePermission(void*, void*, BOOL)
    {
        return NULL;
    }

    /***  See above: This function has no meaning in our shim. */
    WrHandle* STM_AcquireReadWritePermission(void*, void*, BOOL)
    {
        return NULL;
    }

    /**
     *  [mfs] The rest of this file is not correct, but will work for CGL
     */


    /**
     *  Eventually, this will need to call a transactional malloc function.
     */
    void* STM_TranMalloc(void* txid, size_t size)
    {
        return stm::Self->allocator.txAlloc(size);
        // return malloc(size);
    }

    /**
     *  Eventually, this will need to call a transactional free function.
     */
    void STM_TranMFree(void* txid, void* p)
    {
        stm::Self->allocator.txFree(p);
        // free(p);
    }

    /**
     *  Determine if the thread is in a transaction or not, as we only
     *  support STM.  If we had PhTM or HyTM support, then this would need
     *  more complexity to deal with being in a transaction but using HTM
     *  (undecorated) code.
     */
    BOOL STM_CurrentlyUsingDecoratedPath(void* theTransId)
    {
        // if we don't have a descriptor, we can't be in a transaction
        if (theTransId == NULL)
            return 0;
        // if we're not at nesting level 0, we're in a transaction
        stm::TxThread* tx = (stm::TxThread*) theTransId;
        return tx->nesting_depth != 0;
    }

} // extern "C"



// libitm2stm 5-12 provides an implementation of read/write interposition functions

// libitm2stm 5-16 provides for logging stack accesses

// we probably need to implement the following method at some point
//
// void  STM_SelfAbortTransaction(void* theTransId);

// we probably need to implement the following memory management functions
// at some point:
//
// void* STM_TranCalloc(void* theTransId, size_t theNElem, size_t theSize);
// void* STM_TranMemAlign(void* theTransId, size_t theAlignment, size_t theSize);
// void* STM_TranValloc(void* theTransId, size_t theSize);
// void  STM_TranMemCpy(void* theTransId, void* theFromAddr, void* theToAddr, unsigned long theSizeInBytes, UINT32 theAlignment);
