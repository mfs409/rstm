/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#include <iostream>
#include "libitm.h"
#include "tx.hpp"
#include "tmabi.hpp"

using stm::Self;
using stm::TX;

int
_ITM_versionCompatible(int v) {
    // [ld] is this correct? is there any guarantee of backwards compatibility
    //      that would make this an inequality instead?
    return v == _ITM_VERSION_NO;
}

const char*
_ITM_libraryVersion(void) {
    return _ITM_VERSION;
}

void
_ITM_error(const _ITM_srcLocation* src, int) {
    std::cerr << "_ITM_error:" << src->psource << "\n";
    abort();
}

_ITM_howExecuting
_ITM_inTransaction() {
    TX* tx = Self;
    if (!tx->nesting_depth)
        return outsideTransaction;
    return (stm::tm_is_irrevocable(tx)) ? inIrrevocableTransaction :
                                          inRetryableTransaction;
}

_ITM_transactionId_t
_ITM_getTransactionId() {
    // [ld] is this what this call is supposed to do? Do we need to keep a
    //      globally unique id?
    return Self->nesting_depth;
}

/** Used as a restore_checkpoint continuation to cancel a transaction. */
static uint32_t TM_FASTCALL
cancel(uint32_t, TX*) {
    return a_restoreLiveVariables | a_abortTransaction;
}

/** Used as a restore_checkpoint continuation to restart a transaction. */
static uint32_t TM_FASTCALL
restart(uint32_t flags, TX* tx) {
    return stm::tm_begin(flags, tx, a_restoreLiveVariables);
}

void
_ITM_abortTransaction(_ITM_abortReason why) {
    TX* tx = Self;
    if (why & TMConflict) {
        tm_rollback(tx);
        tx->nesting_depth = 1;          // no closed nesting yet.
        restore_checkpoint(restart, tx);
    }
    else if (why & userAbort) {
        if (tx->nesting_depth != 1 && why & ~outerAbort) {
            std::cerr << "RSTM does not yet support cancel inner\n";
            abort();
        }
        tm_rollback(tx);
        restore_checkpoint(cancel, tx);
    }
    else if (why & exceptionBlockAbort) {
        std::cerr << "Exception block aborts are not yet implemented\n";
        abort();
    }
    else {
        std::cerr << "_ITM_abortTransaction called with unhandled reason: "
                  << why << "\n";
        abort();
    }
}

/**
 * From gcc's addendum:
 *
 *    Commit actions will get executed in the same order in which the
 *    respective calls to _ITM_ addUserCommitAction happened. Only
 *    _ITM_noTransactionId is allowed as value for the resumingTransactionId
 *    argument. Commit actions get executed after privatization safety has been
 *    ensured.
*/
void
_ITM_addUserCommitAction(_ITM_userCommitFunction, _ITM_transactionId_t, void*)
{
    assert(false && "Unimplemented");
}

/**
 * From gcc's addendum:
 *
 *    Undo actions will get executed in reverse order compared to the order in
 *    which the respective calls to _ITM_addUserUndoAction happened. The
 *    ordering of undo actions w.r.t. the roll-back of other actions (e.g.,
 *    data transfers or memory allocations) is undefined.
 */
void
_ITM_addUserUndoAction(_ITM_userUndoFunction, void*) {
    assert(false && "Unimplemented");
}

/**
 * From gcc's addendum:
 *
 *    _ITM_dropReferences is not supported currently because its semantics and
 *    the intention behind it is not entirely clear. The specification suggests
 *    that this function is necessary because of certain orderings of data
 *    transfer undos and the releasing of memory regions (i.e.,
 *    privatization). However, this ordering is never defined, nor is the
 *    ordering of dropping references w.r.t. other events.
 */
void
_ITM_dropReferences(void*, size_t) {
    assert(false && "Unimplemented");
}

