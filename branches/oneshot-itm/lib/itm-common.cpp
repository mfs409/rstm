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
#include "inst-common.hpp"              // offset_of, base_of, for _ITM_LB

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
 *  From gcc's addendum:
 *
 *    Commit actions will get executed in the same order in which the
 *    respective calls to _ITM_ addUserCommitAction happened. Only
 *    _ITM_noTransactionId is allowed as value for the resumingTransactionId
 *    argument. Commit actions get executed after privatization safety has been
 *    ensured.
*/
void
_ITM_addUserCommitAction(_ITM_userCommitFunction f, _ITM_transactionId_t,
                         void* a) {
    stm::Self->userCallbacks.doOnCommit(f, a);
}

/**
 *  From gcc's addendum:
 *
 *    Undo actions will get executed in reverse order compared to the order in
 *    which the respective calls to _ITM_addUserUndoAction happened. The
 *    ordering of undo actions w.r.t. the roll-back of other actions (e.g.,
 *    data transfers or memory allocations) is undefined.
 */
void
_ITM_addUserUndoAction(_ITM_userUndoFunction f, void* a) {
    stm::Self->userCallbacks.doOnRollback(f, a);
}

/**
 *  From gcc's addendum:
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

/**
 *  Everyone logs bytes in the exact same way.
 */
void
_ITM_LB(const void* addr, const size_t n) {
    void** const base = stm::base_of(addr);
    const size_t off = stm::offset_of(addr);
    const size_t end = off + n;
    const size_t oflow = (end > sizeof(void*)) ? end % sizeof(void*) : 0;

    // how many words do we need to log?
    // The basic N is just the number of words in n.
    size_t N = (n / sizeof(void*));

    // If there is an offset, then we'll need an extra word.
    N = (off) ? N + 1 : N;

    // If the bytes overflow into a final word, then we'll need another word.
    N = (oflow) ? N + 1 : N;

    // we use the undo_log structure for logging
    TX* tx = Self;

    // log the first word.
    uintptr_t mask = stm::make_mask(off, stm::min(sizeof(void*), end));
    tx->undo_log.insert(base, *base, mask);

    // log any middle words (i < e is necessary because size_t is unsigned)
    for (size_t i = 1, e = N - 1; i < e; ++i)
        tx->undo_log.insert(base + i, base[i], ~0);

    // log the final word
    if (oflow) {
        mask = stm::make_mask(0, oflow);
        tx->undo_log.insert(base + N - 1, base[N - 1], mask);
    }
}
