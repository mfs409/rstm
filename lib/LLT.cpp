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
 *  LLT Implementation
 *
 *    This STM very closely resembles the GV1 variant of TL2.  That is, it uses
 *    orecs and lazy acquire.  Its clock requires everyone to increment it to
 *    commit writes, but this allows for read-set validation to be skipped at
 *    commit time.  Most importantly, there is no in-flight validation: if a
 *    timestamp is greater than when the transaction sampled the clock at begin
 *    time, the transaction aborts.
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

using namespace stm;


/** For querying to get the current algorithm name */
static const char* tm_getalgname() {
    return "LLT";
}

/** Abort and roll back the transaction (e.g., on conflict). */
static checkpoint_t* rollback(TX* tx) {
    ++tx->aborts;

    // release the locks and restore version numbers
    foreach (OrecList, i, tx->locks)
    (*i)->v.all = (*i)->p;

    // undo memory operations, reset lists
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->locks.reset();
    tx->allocator.onTxAbort();
    tx->nesting_depth = 0;
    return &tx->checkpoint;
}

/*** The only metadata we need is a single global padded lock ***/
static pad_word_t timestamp = {0};

/** LLT begin: */
static void tm_begin(scope_t* scope)
{
    TX* tx = Self;
    if (++tx->nesting_depth > 1)
        return;



    tx->allocator.onTxBegin();
    // get a start time
    tx->start_time = timestamp.val;
}

/** LLT validation */
static NOINLINE void validate(TX* tx)
{
    // validate
    foreach (OrecList, i, tx->r_orecs) {
        uintptr_t ivt = (*i)->v.all;
        // if unlocked and newer than start time, abort
        if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
            tm_abort(tx);
    }
}

/** LLT commit (read-only): */
static void tm_end()
{
    TX* tx = Self;
    if (--tx->nesting_depth)
        return;

    if (!tx->writes.size()) {
        // read-only, so just reset lists
        tx->r_orecs.reset();
        tx->allocator.onTxCommit();
        ++tx->commits_ro;
        return;
    }

    // acquire locks
    foreach (WriteSet, i, tx->writes) {
        // get orec, read its version#
        orec_t* o = get_orec(i->addr);
        uintptr_t ivt = o->v.all;

        // lock all orecs, unless already locked
        if (ivt <= tx->start_time) {
            // abort if cannot acquire
            if (!bcasptr(&o->v.all, ivt, tx->my_lock.all))
                tm_abort(tx);
            // save old version to o->p, remember that we hold the lock
            o->p = ivt;
            tx->locks.insert(o);
        }
        // else if we don't hold the lock abort
        else if (ivt != tx->my_lock.all) {
            tm_abort(tx);
        }
    }

    // increment the global timestamp since we have writes
    uintptr_t end_time = 1 + faiptr(&timestamp.val);

    // skip validation if nobody else committed
    if (end_time != (tx->start_time + 1))
        validate(tx);

    // run the redo log
    tx->writes.writeback();

    // release locks
    CFENCE;
    foreach (OrecList, i, tx->locks)
    (*i)->v.all = end_time;

    // clean-up
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->locks.reset();

    tx->allocator.onTxCommit();
    ++tx->commits_rw;
}

/**
 *  LLT read (read-only transaction)
 *
 *    We use "check twice" timestamps in LLT
 */
static TM_FASTCALL
void* tm_read(void** addr) {
    TX* tx = Self;

    if (tx->writes.size()) {
        // check the log for a RAW hazard, we expect to miss
        WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
        bool found = tx->writes.find(log);
        if (found)
            return log.val;
    }

    // get the orec addr
    orec_t* o = get_orec(addr);

    // read orec, then val, then orec
    uintptr_t ivt = o->v.all;
    CFENCE;
    void* tmp = *addr;
    CFENCE;
    uintptr_t ivt2 = o->v.all;
    // if orec never changed, and isn't too new, the read is valid
    if ((ivt <= tx->start_time) && (ivt == ivt2)) {
        // log orec, return the value
        tx->r_orecs.insert(o);
        return tmp;
    }
    // unreachable
    tm_abort(tx);
    return NULL;
}

/** LLT write */
static TM_FASTCALL
void tm_write(void** addr, void* val)
{
    TX* tx = Self;
    tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
}

/**
 *  Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(LLT);
REGISTER_TM_FOR_STANDALONE();
