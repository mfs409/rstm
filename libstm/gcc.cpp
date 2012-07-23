/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 *  License: Modified BSD
 *           Please see the file LICENSE.RSTM for licensing information
 */

#include <iostream>
#include <stdlib.h>
#include <unwind.h>
#include "libitm.h"
#include "txthread.hpp"

#if 0 // NOT USED IN WENJIA BRANCH (YET?)
#include "tmabi.hpp"
#include "inst-common.hpp"              // offset_of, base_of, for _ITM_LB

using stm::TX;
using stm::Self;

#endif

#if 0 // NOT USED IN WENJIA BRANCH (YET?)

extern "C" {

/* Indirect calls. */

/* TODO This is only functional version:
 * it should be rewritten for efficient lookup of clones
 * it should be thread-safe */
struct clone_entry {
    void *orig, *clone;
};

struct clone_table {
    clone_entry *table;
    size_t size;
    clone_table *next;
};

static clone_table *first_clone_table;

void
_ITM_registerTMCloneTable(void *xent, size_t size) {
    clone_entry *ent = (clone_entry *)(xent);
    clone_table *table;

    table = (clone_table *)malloc(sizeof(clone_table));
    table->table = ent;
    table->size = size;

    table->next = first_clone_table;
    first_clone_table = table;
}

void
_ITM_deregisterTMCloneTable(void *xent) {
    clone_entry *ent = (clone_entry *)(xent);
    clone_table **prev;
    clone_table *cur;

    for (prev = &first_clone_table; cur = *prev, cur->table != ent; prev = &cur->next)
        continue;

    *prev = cur->next;
    free(cur);
}

static void *
search_clone_entry(void *ptr) {
    // TODO completely inefficient way to do this.
    // Someone else should rewrite this to satisfy license.

    // Search all tables
    for (clone_table *table = first_clone_table; table ; table = table->next) {
        size_t i;
        // Search all clone entries
        clone_entry *ent = table->table;
        for (i = 0; i < table->size; i++) {
            if (ent[i].orig == ptr)
                return ent[i].clone;
        }
    }
    return NULL;
}

void *
_ITM_getTMCloneOrIrrevocable(void *ptr) {
    if (void *ret = search_clone_entry(ptr))
        return ret;
    // No clone registered, switch to irrevocable
    _ITM_changeTransactionMode(modeSerialIrrevocable);
    return ptr;
}

void *
_ITM_getTMCloneSafe(void *ptr) {
    if (void *ret = search_clone_entry(ptr))
        return ret;
    abort();
}


/* C++ Exception */

extern void *__cxa_allocate_exception (size_t) __attribute__((weak));
extern void __cxa_throw (void *, void *, void *) __attribute__((weak));
extern void *__cxa_begin_catch (void *) __attribute__((weak));
extern void __cxa_end_catch (void) __attribute__((weak));
extern void __cxa_tm_cleanup (void *, void *, unsigned int) __attribute__((weak));

void *
_ITM_cxa_allocate_exception(size_t size) {
    return (Self->cxa_unthrown = __cxa_allocate_exception(size));
}

void
_ITM_cxa_throw(void *obj, void *tinfo, void *dest) {
    Self->cxa_unthrown = NULL;
    __cxa_throw(obj, tinfo, dest);
}

void *
_ITM_cxa_begin_catch(void *exc_ptr) throw() {
    ++Self->cxa_catch_count;
    return __cxa_begin_catch(exc_ptr);
}

void
_ITM_cxa_end_catch(void) {
    --Self->cxa_catch_count;
    __cxa_end_catch();
}

void
exceptionOnAbort(void *exc_ptr) {
    TX* tx = Self;

    if (tx->cxa_unthrown || tx->cxa_catch_count) {
        __cxa_tm_cleanup (tx->cxa_unthrown, exc_ptr, tx->cxa_catch_count);
        tx->cxa_catch_count = 0;
        tx->cxa_unthrown = NULL;
        exc_ptr = NULL;
    }

    if (exc_ptr) {
        _Unwind_DeleteException ((_Unwind_Exception *) exc_ptr);
    }
}

void
_ITM_commitTransactionEH(void *exc_ptr) {
    _ITM_addUserUndoAction(exceptionOnAbort, exc_ptr);
    _ITM_commitTransaction();
    TX* tx = Self;
    tx->cxa_catch_count = 0;
    tx->cxa_unthrown = NULL;

    // td->inner()->registerOnAbort(exceptionOnAbort, exc_ptr);
    // td->commit();
    // /* ??? should not be required for _ITM_commitTransaction */
    // td->TMException.cxa_catch_count = 0;
    // td->TMException.cxa_unthrown = NULL;
}

/* C++ Allocation */

// /* ??? Any portable way to guess mangling name */
// #ifdef __LP64__
// # define MANGLING(A,B) A##m##B
// #else /* ! __LP64__ */
// # define MANGLING(A,B) A##j##B
// #endif /* ! __LP64__ */

// typedef const struct nothrow_t { } *c_nothrow_p;
// extern void *MANGLING(_Znw,) (size_t) __attribute__((weak));
// extern void *MANGLING(_Zna,) (size_t) __attribute__((weak));
// extern void *MANGLING(_Znw,RKSt9nothrow_t) (size_t, c_nothrow_p) __attribute__((weak));
// extern void *MANGLING(_Zna,RKSt9nothrow_t) (size_t, c_nothrow_p) __attribute__((weak));

// extern void _ZdlPv (void *) __attribute__((weak));
// extern void _ZdaPv (void *) __attribute__((weak));
// extern void _ZdlPvRKSt9nothrow_t (void *, c_nothrow_p) __attribute__((weak));
// extern void _ZdaPvRKSt9nothrow_t (void *, c_nothrow_p) __attribute__((weak));

// void *
// MANGLING(_ZGTtnw,)(size_t sz) {
//     _ITM_TD_GET;
//     void *ptr = MANGLING(_Znw,)(sz);
//     td->inner()->registerOnAbort(_ZdlPv, ptr);
//     return ptr;
// }

// void *
// MANGLING(_ZGTtna,)(size_t sz) {
//     _ITM_TD_GET;
//     void *ptr = MANGLING(_Zna,)(sz);
//     td->inner()->registerOnAbort(_ZdaPv, ptr);
//     return ptr;
// }

// static void
// _ZdlPvRKSt9nothrow_t1(void *ptr) {
//     _ZdlPvRKSt9nothrow_t (ptr, NULL);
// }

// void *
// MANGLING(_ZGTtnw,RKSt9nothrow_t)(size_t sz, c_nothrow_p nt) {
//     _ITM_TD_GET;
//     void *ptr = MANGLING(_Znw,RKSt9nothrow_t)(sz, nt);
//     td->inner()->registerOnAbort(_ZdlPvRKSt9nothrow_t1, ptr);
//     return ptr;
// }

// static void
// _ZdaPvRKSt9nothrow_t1(void *ptr) {
//     _ZdaPvRKSt9nothrow_t(ptr, NULL);
// }

// void *
// MANGLING(_ZGTtna,RKSt9nothrow_t)(size_t sz, c_nothrow_p nt) {
//     _ITM_TD_GET;
//     void *ptr = MANGLING(_Zna,RKSt9nothrow_t)(sz, nt);
//     td->inner()->registerOnAbort(_ZdaPvRKSt9nothrow_t1, ptr);
//     return ptr;
// }

// void
// _ZGTtdlPv(void *ptr) {
//     _ITM_TD_GET;
//     td->inner()->registerOnCommit(_ZdlPv, ptr);
// }

// void
// _ZGTtdlPvRKSt9nothrow_t(void *ptr, c_nothrow_p nt) {
//     _ITM_TD_GET;
//     td->inner()->registerOnCommit(_ZdlPvRKSt9nothrow_t1, ptr);
// }

// void
// _ZGTtdaPv(void *ptr) {
//     _ITM_TD_GET;
//     td->inner()->registerOnCommit(_ZdaPv, ptr);
// }

// void
// _ZGTtdaPvRKSt9nothrow_t(void *ptr, c_nothrow_p nt) {
//     _ITM_TD_GET;
//     td->inner()->registerOnCommit(_ZdaPvRKSt9nothrow_t1, ptr);
// }

// #undef MANGLING

} // extern "C"


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

#endif

namespace stm
{
  void begin(scope_t* s, uint32_t);
}

/** Used as a restore_checkpoint continuation to restart a transaction. */
static uint32_t TM_FASTCALL restart(uint32_t flags)
{
    // NB: it would be essentially free to pass the descriptor as a second
    //     parameter to tmbegin, because we could pass it to this function
    //     for free...
    stm::tmbegin();
}

NORETURN
void stm::TxThread::tmabort()
{
    // [mfs] this is a hack for now.  With compiler support, 'why' should
    //       become a parameter
    _ITM_abortReason why = TMConflict;

    stm::TxThread* tx = stm::Self;
    if (why & TMConflict) {
        stm::TxThread::tmrollback(tx);
        tx->nesting_depth = 1;          // no closed nesting yet.
        restore_checkpoint(restart);
    }
#if 0 // NOT USED IN WENJIA BRANCH (YET?)
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
#endif
    else {
        std::cerr << "_ITM_abortTransaction called with unhandled reason: "
                  << why << "\n";
        abort();
    }
    abort();
}

#if 0 // NOT USED IN WENJIA BRANCH (YET?)
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
    const size_t N = (n / sizeof(void*)) + ((off) ? 1 : 0) + ((oflow) ? 1 : 0);

    // we use the undo_log structure for logging
    TX* tx = Self;

    // log the first word.
    uintptr_t mask = stm::make_mask(off, stm::min(sizeof(void*), end));
    tx->undo_log.insert(base, *base, mask);

    // log any middle words
    for (size_t i = 1, e = N - 1; i < e; ++i)
        tx->undo_log.insert(base + i, base[i], ~0);

    // log the final word
    if (N > 1) {
        mask = stm::make_mask(0, (oflow) ? oflow : sizeof(void*));
        tx->undo_log.insert(base + N - 1, base[N - 1], mask);
    }
}
#endif
