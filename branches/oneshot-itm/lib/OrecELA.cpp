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
 *  OrecELA Implementation
 *
 *    This is similar to the Detlefs algorithm for privatization-safe STM,
 *    TL2-IP, and [Marathe et al. ICPP 2008].  We use commit time ordering to
 *    ensure that there are no delayed cleanup problems, we poll the timestamp
 *    variable to address doomed transactions, but unlike the above works, we
 *    use TinySTM-style extendable timestamps instead of TL2-style timestamps,
 *    which sacrifices some publication safety.
 */

#include <iostream>
#include "byte-logging.hpp"
#include "tmabi-weak.hpp"
#include "foreach.hpp"
#include "inst3.hpp"                    // read<>/write<?>, etc
#include "MiniVector.hpp"
#include "metadata.hpp"
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
    return "OrecELA";
}

/*** The only metadata we need is a single global padded lock ***/
static pad_word_t timestamp = {0};
static pad_word_t last_complete = {0};

/**
 *  OrecELA unwinder:
 *
 *    This is a standard orec unwind function.  The only catch is that if a
 *    transaction aborted after incrementing the timestamp, it must wait its
 *    turn and then increment the trailing timestamp, to keep the two counters
 *    consistent.
 */
void alg_tm_rollback(TX* tx)
{
    ++tx->aborts;

    // release locks and restore version numbers
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
    //
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
 *  OrecELA begin:
 *
 *    We need a starting point for the transaction.  If an in-flight
 *    transaction is committed, but still doing writeback, we can either start
 *    at the point where that transaction had not yet committed, or else we can
 *    wait for it to finish writeback.  In this code, we choose the former
 *    option.
 *
 *    only called for outermost transactions.
 */
uint32_t alg_tm_begin(uint32_t, TX* tx)
{
    tx->allocator.onTxBegin();
    // Start after the last cleanup, instead of after the last commit, to
    // avoid spinning in begin()
    tx->start_time = last_complete.val;
    tx->end_time = 0;
    return a_runInstrumentedCode;
}

static NOINLINE void validate_commit(TX* tx)
{
    FOREACH (OrecList, i, tx->r_orecs) {
        // read this orec
        uintptr_t ivt = (*i)->v.all;
        if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
            _ITM_abortTransaction(TMConflict);
    }
}

/**
 *  OrecELA commit (read-only):
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
        orec_t* o = get_orec(i->address);
        uintptr_t ivt = o->v.all;

        // if orec not locked, lock it and save old to orec.p
        if (ivt <= tx->start_time) {
            // abort if cannot acquire
            if (!bcasptr(&o->v.all, ivt, tx->my_lock.all))
                _ITM_abortTransaction(TMConflict);
            // save old version to o->p, log lock
            o->p = ivt;
            tx->locks.insert(o);
        }
        // else if we don't hold the lock abort
        else if (ivt != tx->my_lock.all) {
            _ITM_abortTransaction(TMConflict);
        }
    }
    CFENCE;
    // increment the global timestamp if we have writes
    tx->end_time = 1 + faiptr(&timestamp.val);
    CFENCE;
    // skip validation if possible
    if (tx->end_time != (tx->start_time + 1)) {
        validate_commit(tx);
    }
    CFENCE;
    // run the redo log
    tx->writes.redo();
    CFENCE;
    // release locks
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
 *  OrecELA validation
 *
 *    an in-flight transaction must make sure it isn't suffering from the
 *    "doomed transaction" half of the privatization problem.  We can get that
 *    effect by calling this after every transactional read (actually every
 *    read that detects that some new transaction has committed).
 */
static NOINLINE void privtest(TX* tx, uintptr_t ts)
{
    // optimized validation since we don't hold any locks
    FOREACH (OrecList, i, tx->r_orecs) {
        // if orec locked or newer than start time, abort
        if ((*i)->v.all > tx->start_time)
            _ITM_abortTransaction(TMConflict);
    }
    // careful here: we can't scale the start time past last_complete.val,
    // unless we want to re-introduce the need for prevalidation on every
    // read.
    CFENCE;
    uintptr_t cs = last_complete.val;
    tx->start_time = (ts < cs) ? ts : cs;
}

namespace {
/**
 *  OrecELA read (read-only transaction)
 *
 *    This is a traditional orec read for systems with extendable timestamps.
 *    However, we also poll the timestamp counter and validate any time a new
 *    transaction has committed, in order to catch doomed transactions.
 */
  struct Read {
      void* operator()(void** addr, TX* tx, uintptr_t) const {
          // get the orec addr, read the orec's version#
          orec_t* o = get_orec(addr);
          while (true) {
              // read the location
              void* tmp = *addr;
              CFENCE;
              // check the orec.  Note: we don't need prevalidation because we
              // have a global clean state via the last_complete.val field.
              id_version_t ivt;
              ivt.all = o->v.all;

              // common case: new read to uncontended location
              if (ivt.all <= tx->start_time) {
                  tx->r_orecs.insert(o);
                  // privatization safety: avoid the "doomed transaction" half
                  // of the privatization problem by polling a global and
                  // validating if necessary
                  uintptr_t ts = timestamp.val;
                  CFENCE;
                  if (ts != tx->start_time)
                      privtest(tx, ts);
                  return tmp;
              }

              // if lock held, spin and retry
              if (ivt.fields.lock) {
                  spin64();
                  continue;
              }

              // unlocked but too new... validate and scale forward
              uintptr_t newts = timestamp.val;
              FOREACH (OrecList, i, tx->r_orecs) {
                  // if orec locked or newer than start time, abort
                  if ((*i)->v.all > tx->start_time)
                      _ITM_abortTransaction(TMConflict);
              }
              CFENCE;
              uintptr_t cs = last_complete.val;
              // need to pick cs or newts
              tx->start_time = (newts < cs) ? newts : cs;
          }
      }
  };
}

void* alg_tm_read(void** addr) {
    return Lazy<void*, Read>::RSTM::Read(addr);
}

void alg_tm_write(void** addr, void* val) {
    Lazy<void*, Read>::RSTM::Write(addr, val);
}

bool alg_tm_is_irrevocable(TX*) {
    assert(false && "Unimplemented");
    return false;
}

void alg_tm_become_irrevocable(_ITM_transactionState) {
    assert(false && "Unimplemented");
    return;
}

/**
 * Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(OrecELA)

/**
 *  Instantiate our read template for all of the read types, and add weak
 *  aliases for the LIBITM symbols to them.
 *
 *  TODO: We can't make weak aliases without mangling the symbol names, but
 *        this is non-trivial for the instrumentation templates. For now, we
 *        just inline the read templates into weak versions of the library. We
 *        could use gcc's asm() exetension to instantiate the template with a
 *        reasonable name...?
 */
#define RSTM_LIBITM_READ(SYMBOL, CALLING_CONVENTION, TYPE)              \
    TYPE CALLING_CONVENTION __attribute__((weak)) SYMBOL(TYPE* addr) {  \
        return Lazy<TYPE, Read>::ITM::Read(addr);                       \
    }

#define RSTM_LIBITM_WRITE(SYMBOL, CALLING_CONVENTION, TYPE)             \
    void CALLING_CONVENTION __attribute__((weak))                       \
        SYMBOL(TYPE* addr, TYPE val) {                                  \
        Lazy<TYPE, Read>::ITM::Write(addr, val);                        \
    }

#include "libitm-dtfns.def"

#undef RSTM_LIBITM_WRITE
#undef RSTM_LIBITM_READ
