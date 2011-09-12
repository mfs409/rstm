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
#include "stm/txthread.hpp"

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
    BOOL  STM_BeginTransaction(void* theTransId)
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
    BOOL  STM_ValidateTransaction(void* theTransId)
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
    CommitStatus  STM_CommitTransaction(void* theTransId)
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
     *  [mfs] The rest of this file is not correct, but will work for CGL
     */

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

/*

  look at 5-12 for barriers
  5-16 has logging stuff
  5-7 has begin
  5-9 has commit

    void  STM_SelfAbortTransaction(void* theTransId);
    BOOL  STM_CurrentlyUsingDecoratedPath(void* theTransId);

    WrHandle* STM_AcquireReadWritePermission
    (void* theTransId, void* theAddr, BOOL theValid);

    UINT8  STM_TranRead8
    (void* theTransId, RdHandle* theRdHandle, UINT8 * theAddr, BOOL theValid);
    UINT16 STM_TranRead16
    (void* theTransId, RdHandle* theRdHandle, UINT16* theAddr, BOOL theValid);

    UINT64 STM_TranRead64
    (void* theTransId, RdHandle* theRdHandle, UINT64* theAddr, BOOL theValid);
    float  STM_TranReadFloat32
    (void* theTransId, RdHandle* theRdHandle, float * theAddr, BOOL theValid);
    double STM_TranReadFloat64

    (void* theTransId, RdHandle* theRdHandle, double* theAddr, BOOL theValid);
    BOOL  STM_TranWrite8
    (void* theTransId, WrHandle* theWrHandle, UINT8 * theAddr,  UINT8 theVal, BOOL theValid);
    BOOL  STM_TranWrite16
    (void* theTransId, WrHandle* theWrHandle, UINT16* theAddr, UINT16 theVal, BOOL theValid);

    BOOL  STM_TranWrite64
    (void* theTransId, WrHandle* theWrHandle, UINT64* theAddr, UINT64 theVal, BOOL theValid);
    BOOL  STM_TranWriteFloat32
    (void* theTransId, WrHandle* theWrHandle, float * theAddr,  float theVal, BOOL theValid);
    BOOL  STM_TranWriteFloat64
    (void* theTransId, WrHandle* theWrHandle, double* theAddr, double theVal, BOOL theValid);

    void* STM_TranCalloc(void* theTransId, size_t theNElem, size_t theSize);
    void* STM_TranMemAlign(void* theTransId, size_t theAlignment, size_t theSize);
    void* STM_TranValloc(void* theTransId, size_t theSize);
    void  STM_TranMemCpy(void* theTransId, void* theFromAddr, void* theToAddr,
                         unsigned long theSizeInBytes, UINT32 theAlignment);

 */
