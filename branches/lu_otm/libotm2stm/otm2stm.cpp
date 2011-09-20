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
 *  Extremely lightweight "shim" for translating Oracle TM instrumentation
 *  into RSTM instrumentation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <common/platform.hpp>
#include <stm/txthread.hpp>
#include "alt-license/OracleSkyStuff.hpp"
#include "common/locks.hpp"
#include "stm/metadata.hpp"
#include "stm/StackLogger.hpp"

#include "OTM_Inliner.i"

namespace stm
{
  void sys_init(void (*abort_handler)(TxThread*) = NULL);
}

/**
 *  In OTM, the compiler adds instrumentation to manually unwinde the
 *  transaction, one stack frame at a time.  This makes sense (especially on
 *  SPARC or for transactions with no function calls and few accesses), but
 *  it's bad for RSTM, because RSTM assumes setjmp/longjmp unwinding (or its
 *  equivalent).  We don't want to rewrite all our algorithms to support dual
 *  mechanisms for unwind, so instead we use a macro and this code at begin
 *  time.
 *
 *  The macro (TM_BEGIN, in cxxtm.hpp) performs a setjmp, calls this, and
 *  then invokes the __transaction construct.  In this way, we checkpoint
 *  the current stack, and then before actually starting the transaction,
 *  this code will determine if the jump buffer needs to be saved (and
 *  write-read ordering enforced), and if so, it will do that work, which
 *  is essentially half of the begin method from library.hpp
 *
 *  BITROT WARNING: this can easily fall out of sync with library.hpp.  We
 *                  should come up with away to address the redundancy
 */
void OTM_PREBEGIN(stm::scope_t* s)
{
    // get the descriptor, and if null, initialize it
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
     *  transaction's descriptor.  Since RSTM already maintains the pointer,
     *  we just forward to RSTM
     *
     *  [mfs] Need to inline this eventually.
     */
    void* STM_GetMyTransId() { return (stm::TxThread*)stm::Self; }

    /**
     *  To begin a transaction, we use a macro that does half of the work of
     *  RSTM's begin transaction.  Then the compiler calls this code, which
     *  is where we put the "other half" of the begin function.  This is just
     *  a call to the begin function pointer.
     *
     *  BITROT WARNING: this can easily fall out of sync with library.hpp.
     *                  We should come up with away to address the redundancy
     *
     *  [mfs] Need to inline this eventually.
     */
    BOOL STM_BeginTransaction(void* theTransId)
    {
        stm::TxThread* tx = (stm::TxThread*)theTransId;

        // some adaptivity mechanisms need to know nontransactional and
        // transactional time.  This code suffices, because it gets the time
        // between transactions.  If we need the time for a single transaction,
        // we can run ProfileTM
        if (tx->end_txn_time)
            tx->total_nontxn_time += (tick() - tx->end_txn_time);

        // now call the per-algorithm begin function
        stm::TxThread::tmbegin(tx);
        // since we use setjmp/longjmp, this function always returns, and can
        // return 1 safely
        //
        // [mfs] we will need to revisit this claim if we are to support
        //       CANCEL
        return 1;
    }

    /**
     *  The commit logic has two parts.  First, we handle nesting, then we
     *  actually call the per-algorithm commit function.  In RSTM, the first
     *  part is inlined, but in this shim, for now, we use this function.
     *
     *  BITROT WARNING: this can easily fall out of sync with library.hpp.
     *                  We should come up with away to address the redundancy
     *
     *  [mfs] Need to inline this eventually.
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

        return CommittedNoRetry;
    }

    /**
     *  RSTM already does a pretty good job of handling allocations via a
     *  single call that works inside and outside of transactions.  The
     *  Oracle API expects a function that does the same, so we can just
     *  forward to the RSTM code.
     */
    void* STM_TranMalloc(void* txid, size_t size)
    {
        return stm::Self->allocator.txAlloc(size);
    }

    /**
     *  See STM_TranMalloc: we just forward to RSTM's free code
     */
    void STM_TranMFree(void* txid, void* p)
    {
        stm::Self->allocator.txFree(p);
    }

    /**
     *  The compiler needs to know what version of a transaction body to
     *  call: the version with instrumentation or the version without it.  In
     *  our case, we use "with" if we are transactional, and "without"
     *  otherwise.  Hardware TM would necessitate more cleverness.
     *
     *  [mfs] If we were more nuanced, we'd be able to track if we were using
     *        CGL or not, and generate two different paths through the code,
     *        one with instrumentation and the other without.  That, of
     *        course, doesn't quite work with CANCEL, which we don't support
     *        yet anyway.  Some day...
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

// [mfs] we probably need to implement the following method at some point
//
// void  STM_SelfAbortTransaction(void* theTransId);
//
// void* STM_TranCalloc(void* theTransId, size_t theNElem, size_t theSize);
// void* STM_TranMemAlign(void* theTransId, size_t theAlignment, size_t theSize);
// void* STM_TranValloc(void* theTransId, size_t theSize);
// void  STM_TranMemCpy(void* theTransId, void* theFromAddr, void* theToAddr, unsigned long theSizeInBytes, UINT32 theAlignment);
