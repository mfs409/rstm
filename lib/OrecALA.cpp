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
 *  OrecALA Implementation
 *
 *    This is similar to the Detlefs algorithm for privatization-safe STM,
 *    TL2-IP, and [Marathe et al. ICPP 2008].  We use commit time ordering to
 *    ensure that there are no delayed cleanup problems, and we poll the
 *    timestamp variable to address doomed transactions.  By using TL2-style
 *    timestamps, we also achieve ALA publication safety
 */

#include <iostream>
#include "tmabi-weak.hpp"
#include "foreach.hpp"
#include "MiniVector.hpp"
#include "metadata.hpp"
#include "WriteSet.hpp"
#include "WBMMPolicy.hpp"
#include "locks.hpp"
#include "tx.hpp"
#include "adaptivity.hpp"
#include "tm_alloc.hpp"
#include "libitm.h"

using namespace stm;

/**
 *  For querying to get the current algorithm name
 */
const char* alg_tm_getalgname() {
    return "OrecALA";
}

/*** The only metadata we need is a single global padded lock ***/
static pad_word_t timestamp = {0};
static pad_word_t last_complete = {0};

/**
 *  OrecALA rollback:
 *
 *    This is a standard orec unwind function.  The only catch is that if a
 *    transaction aborted after incrementing the timestamp, it must wait its
 *    turn and then increment the trailing timestamp, to keep the two counters
 *    consistent.
 */
void alg_tm_rollback(TX* tx)
{
    ++tx->aborts;


    // release the locks and restore version numbers
    FOREACH (OrecList, i, tx->locks) {
        (*i)->v.all = (*i)->p;
    }
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->locks.reset();

    CFENCE;

    // if we aborted after incrementing the timestamp, then we have to
    // participate in the global cleanup order to support our solution to
    // the deferred update half of the privatization problem.
    // NB:  Note that end_time is always zero for restarts and retrys
    if (tx->end_time != 0) {
        while (last_complete.val < (tx->end_time - 1))
            spin64();
        CFENCE;
        last_complete.val = tx->end_time;
    }
    CFENCE;
    tx->allocator.onTxAbort();
}

/**
 *  OrecALA begin:
 *
 *    We need a starting point for the transaction.  If an in-flight
 *    transaction is committed, but still doing writeback, we can either start
 *    at the point where that transaction had not yet committed, or else we can
 *    wait for it to finish writeback.  In this code, we choose the former
 *    option.
 *
 *    NB: the latter option might be better, since there is no timestamp
 *        scaling
 *        only called for outermost transactions.
 */
uint32_t alg_tm_begin(uint32_t, TX* tx)
{
    tx->allocator.onTxBegin();

    // Start after the last cleanup, instead of after the last commit, to
    // avoid spinning in begin()
    tx->start_time = last_complete.val;
    tx->ts_cache = tx->start_time;
    tx->end_time = 0;
    return a_runInstrumentedCode;
}

static NOINLINE void validate_commit(TX* tx)
{
    FOREACH (OrecList, i, tx->r_orecs) {
        // read this orec
        uintptr_t ivt = (*i)->v.all;
        if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
            tm_abort(tx);
    }
}

/**
 *  OrecALA commit (read-only):
 *
 *    RO commit is trivial
 */
void alg_tm_end()
{
    TX* tx = Self;
    if (--tx->nesting_depth)
        return;

    CFENCE;
    if (!tx->writes.size()) {
        tx->r_orecs.reset();
        tx->allocator.onTxCommit();
        ++tx->commits_ro;
        return;
    }

    // acquire locks
    FOREACH (WriteSet, i, tx->writes) {
        // get orec, read its version#
        orec_t* o = get_orec(i->addr);
        uintptr_t ivt = o->v.all;

        // if orec not locked, lock it and save old to orec.p
        if (ivt <= tx->start_time) {
            // abort if cannot acquire
            if (!bcasptr(&o->v.all, ivt, tx->my_lock.all))
                tm_abort(tx);
            // save old version to o->p, remember that we hold the lock
            o->p = ivt;
            tx->locks.insert(o);
        }
        else if (ivt != tx->my_lock.all) {
            tm_abort(tx);
        }
    }
    CFENCE;

    // increment the global timestamp
    tx->end_time = 1 + faiptr(&timestamp.val);
    CFENCE;
    // skip validation if nobody committed since my last validation
    if (tx->end_time != (tx->ts_cache + 1)) {
        validate_commit(tx);
    }
    CFENCE;
    // run the redo log
    tx->writes.writeback();

    // release locks
    CFENCE;
    FOREACH (OrecList, i, tx->locks) {
        (*i)->v.all = tx->end_time;
    }
    CFENCE;
    // now ensure that transactions depart from stm_end in the order that
    // they incremend the timestamp.  This avoids the "deferred update"
    // half of the privatization problem.
    while (last_complete.val != (tx->end_time - 1))
        spin64();
    last_complete.val = tx->end_time;

    // clean-up
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->locks.reset();
    tx->allocator.onTxCommit();
    ++tx->commits_rw;
}

/**
 *  OrecALA validation
 *
 *    an in-flight transaction must make sure it isn't suffering from the
 *    "doomed transaction" half of the privatization problem.  We can get that
 *    effect by calling this after every transactional read.
 */
static NOINLINE void privtest(TX* tx, uintptr_t ts)
{
    // optimized validation since we don't hold any locks
    FOREACH (OrecList, i, tx->r_orecs) {
        // if orec unlocked and newer than start time, it changed, so abort.
        // if locked, it's not locked by me so abort
        if ((*i)->v.all > tx->start_time)
            tm_abort(tx);
    }

    // remember that we validated at this time
    tx->ts_cache = ts;
}

/**
 *  OrecALA read (read-only transaction)
 *
 *    Standard tl2-style read, but then we poll for potential privatization
 *    conflicts
 */
void* alg_tm_read(void** addr)
{
    TX* tx = Self;

    if (tx->writes.size()) {
        // check the log for a RAW hazard, we expect to miss
        WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
        bool found = tx->writes.find(log);
        if (found)
            return log.val;
    }

    // read the location, log the orec
    void* tmp = *addr;
    orec_t* o = get_orec(addr);
    tx->r_orecs.insert(o);
    CFENCE;

    // make sure this location isn't locked or too new
    if (o->v.all > tx->start_time)
        tm_abort(tx);

    // privatization safety: poll the timestamp, maybe validate
    uintptr_t ts = timestamp.val;
    if (ts != tx->ts_cache)
        privtest(tx, ts);
    // return the value we read
    return tmp;
}

/**
 *  OrecALA write (read-only context)
 *
 *    Buffer the write, and switch to a writing context.
 */
void alg_tm_write(void** addr, void* val)
{
    TX* tx = Self;
    tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
}

bool alg_tm_is_irrevocable(TX*) {
    assert(false && "Unimplemented");
    return false;
}

/**
 * Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(OrecALA)
