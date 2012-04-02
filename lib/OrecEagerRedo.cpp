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
 *  OrecEagerRedo Implementation
 *
 *    This code is very similar to the TinySTM-writeback algorithm.  It can
 *    also be thought of as OrecEager with redo logs instead of undo logs.
 *    Note, though, that it uses timestamps as in Wang's CGO 2007 paper, so
 *    we always validate at commit time but we don't have to check orecs
 *    twice during each read.
 */

#include <iostream>
#include <setjmp.h>
#include "MiniVector.hpp"
#include "metadata.hpp"
#include "WriteSet.hpp"
#include "WBMMPolicy.hpp"
#include "Macros.hpp"
#include "tx.hpp"
#include "adaptivity.hpp"
#include "tm_alloc.hpp"
#include "libitm.h"

using namespace stm;

/**
 *  For querying to get the current algorithm name
 */
static const char* tm_getalgname() {
    return "OrecEagerRedo";
}

/**
 *  OrecEagerRedo unwinder:
 *
 *    To unwind, we must release locks, but we don't have an undo log to run.
 */
static checkpoint_t* rollback(TX* tx)
{
    ++tx->aborts;

    // release the locks and restore version numbers
    foreach (OrecList, i, tx->locks)
    (*i)->v.all = (*i)->p;

    // undo memory operations, reset lists
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->locks.reset();
    tx->allocator.onTxAbort();
    return &tx->checkpoint;
}

/*** The only metadata we need is a single global padded lock ***/
static pad_word_t timestamp = {0};

/**
 *  OrecEagerRedo begin:
 *
 *    Standard begin: just get a start time, only called for outermost
 *    transactions.
 */
static uint32_t tm_begin(uint32_t, TX* tx)
{
    tx->allocator.onTxBegin();
    tx->start_time = timestamp.val;
    return a_runInstrumentedCode;
}

/**
 *  OrecEagerRedo validation
 *
 *    validate the read set by making sure that all orecs that we've read have
 *    timestamps older than our start time, unless we locked those orecs.
 */
static NOINLINE void validate(TX* tx)
{
    foreach (OrecList, i, tx->r_orecs) {
        // read this orec
        uintptr_t ivt = (*i)->v.all;
        // if unlocked and newer than start time, abort
        if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
            tm_abort(tx);
    }
}

/**
 *  OrecEagerRedo commit (read-only):
 *
 *    Standard commit: we hold no locks, and we're valid, so just clean up
 */
static void tm_end()
{
    TX* tx = Self;
    if (--tx->nesting_depth)
        return;

    if (!tx->writes.size()) {
        tx->r_orecs.reset();
        tx->allocator.onTxCommit();
        ++tx->commits_ro;
        return;
    }

    // note: we're using timestamps in the same manner as
    // OrecLazy... without the single-thread optimization

    // we have all locks, so validate
    foreach (OrecList, i, tx->r_orecs) {
        // read this orec
        uintptr_t ivt = (*i)->v.all;
        // if unlocked and newer than start time, abort
        if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
            tm_abort(tx);
    }

    // run the redo log
    tx->writes.writeback();

    // we're a writer, so increment the global timestamp
    uintptr_t end_time = 1 + faiptr(&timestamp.val);

    // release locks
    foreach (OrecList, i, tx->locks)
    (*i)->v.all = end_time;

    // clean up
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->locks.reset();
    tx->allocator.onTxCommit();
    ++tx->commits_rw;
}

/**
 *  OrecEagerRedo read
 */
static TM_FASTCALL void* tm_read(void** addr)
{
    TX* tx = Self;

    // get the orec addr
    orec_t* o = get_orec(addr);
    while (true) {
        // read the location
        void* tmp = *addr;
        CFENCE;
        // read orec
        id_version_t ivt; ivt.all = o->v.all;

        // common case: new read to uncontended location
        if (ivt.all <= tx->start_time) {
            tx->r_orecs.insert(o);
            return tmp;
        }

        // next best: locked by me
        if (ivt.all == tx->my_lock.all) {
            // check the log for a RAW hazard, we expect to miss
            WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
            bool found = tx->writes.find(log);
            if (found)
                return log.val;
            return *addr;
        }

        // abort if locked by other
        if (ivt.fields.lock)
            tm_abort(tx);

        // scale timestamp if ivt is too new
        uintptr_t newts = timestamp.val;
        validate(tx);
        tx->start_time = newts;
    }
}

/**
 *  OrecEagerRedo write
 */
static TM_FASTCALL void tm_write(void** addr, void* val)
{
    TX* tx = Self;

    // add to redo log
    tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

    // get the orec addr
    orec_t* o = get_orec(addr);
    while (true) {
        // read the orec version number
        id_version_t ivt;
        ivt.all = o->v.all;

        // common case: uncontended location... lock it
        if (ivt.all <= tx->start_time) {
            if (!bcasptr(&o->v.all, ivt.all, tx->my_lock.all))
                tm_abort(tx);

            // save old, log lock, write, return
            o->p = ivt.all;
            tx->locks.insert(o);
            return;
        }

        // next best: already have the lock
        if (ivt.all == tx->my_lock.all)
            return;

        // fail if lock held
        if (ivt.fields.lock)
            tm_abort(tx);

        // unlocked but too new... scale forward and try again
        uintptr_t newts = timestamp.val;
        validate(tx);
        tx->start_time = newts;
    }
}

/**
 * Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(OrecEagerRedo)
REGISTER_TM_FOR_STANDALONE();
