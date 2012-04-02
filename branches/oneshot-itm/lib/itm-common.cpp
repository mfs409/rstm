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

int _ITM_versionCompatible(int v) {
    // [ld] is this correct? is there any guarantee of backwards compatibility
    //      that would make this an inequality instead?
    return v == _ITM_VERSION_NO;
}

const char* _ITM_libraryVersion(void) {
    return _ITM_VERSION;
}

void _ITM_error(const _ITM_srcLocation* src, int) {
    std::cerr << "_ITM_error:" << src->psource << "\n";
    abort();
}

_ITM_howExecuting _ITM_inTransaction() {
    TX* tx = Self;
    if (!tx->nesting_depth)
        return outsideTransaction;
    return (stm::tm_is_irrevocable(tx)) ? inIrrevocableTransaction :
                                          inRetryableTransaction;
}

_ITM_transactionId_t _ITM_getTransactionId() {
    // [ld] is this what this call is supposed to do? Do we need to keep a
    //      globally unique id?
    return Self->nesting_depth;
}

/** Used aas a restore_checkpoint continuation to cancel a transaction. */
static uint32_t TM_FASTCALL cancel(uint32_t, TX*) {
    return a_restoreLiveVariables | a_abortTransaction;
}

void _ITM_abortTransaction(_ITM_abortReason why) {
    TX* tx = Self;
    if (why & TMConflict) {
        tm_rollback(tx);
        tx->nesting_depth = 1;          // no closed nesting yet.
        restore_checkpoint(stm::tm_begin, tx);
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
