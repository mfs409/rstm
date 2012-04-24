/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 *  License: Modified BSD
 *           Please see the file LICENSE.RSTM for licensing information
 */

#include <stdlib.h>
#include <unwind.h>
#include "libitm.h"
#include "tx.hpp"

using stm::TX;
using stm::Self;

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
extern void *__cxa_end_catch (void) __attribute__((weak));
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
_ITM_cxa_begin_catch(void *exc_ptr) {
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

