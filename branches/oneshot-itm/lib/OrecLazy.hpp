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
 *  OrecLazy Implementation:
 *
 *    This STM is similar to the commit-time locking variant of TinySTM.  It
 *    also resembles the "patient" STM published by Spear et al. at PPoPP 2009.
 *    The key difference deals with the way timestamps are managed.  This code
 *    uses the manner of timestamps described by Wang et al. in their CGO 2007
 *    paper.  More details can be found in the OrecEager implementation.
 */

#include <iostream>
#include "byte-logging.hpp"
#include "tmabi-weak.hpp"
#include "foreach.hpp"
#include "inst.hpp"                    // read<>/write<>, etc
#include "MiniVector.hpp"
#include "metadata.hpp"
#include "WBMMPolicy.hpp"
#include "locks.hpp"
#include "tx.hpp"
#include "libitm.h"

using namespace stm;

/**
 *  OrecLazy unwinder:
 *
 *    To unwind, we must release locks, but we don't have an undo log to run.
 */
template <class CM>
static void alg_tm_rollback(TX* tx)
{
    ++tx->aborts;

    // release the locks and restore version numbers
    FOREACH (OrecList, i, tx->locks) {
        (*i)->v.all = (*i)->p;
    }

    tx->undo_log.undo();                // ITM _ITM_LOG support

    // undo memory operations, reset lists
    CM::onAbort(tx);
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->locks.reset();
    tx->allocator.onTxAbort();
    tx->userCallbacks.onRollback();
}

/*** The only metadata we need is a single global padded lock ***/
static pad_word_t timestamp = {0};

/**
 *  OrecLazy begin:
 *
 *    Standard begin: just get a start time. only called for outermost
 *    transactions.
 */
template <class CM>
static uint32_t TM_FASTCALL
alg_tm_begin(uint32_t, TX* tx, uint32_t extra) {
    CM::onBegin(tx);
    tx->allocator.onTxBegin();
    tx->start_time = timestamp.val;
    return extra | a_runInstrumentedCode;
}

/**
 *  OrecLazy validation
 *
 *    validate the read set by making sure that all orecs that we've read have
 *    timestamps older than our start time, unless we locked those orecs.
 */
static void NOINLINE
validate(TX* tx) {
    FOREACH (OrecList, i, tx->r_orecs) {
        // abort if orec locked, or if unlocked but timestamp too new
        if ((*i)->v.all > tx->start_time)
            _ITM_abortTransaction(TMConflict);
    }
}

/**
 *  OrecLazy commit (read-only):
 *
 *    Standard commit: we hold no locks, and we're valid, so just clean up
 */
template <class CM>
static void
alg_tm_end() {
    TX* tx = Self;
    if (--tx->nesting_depth)
        return;

    if (!tx->writes.size()) {
        tx->undo_log.reset();           // ITM _ITM_LOG support
        tx->r_orecs.reset();
        tx->allocator.onTxCommit();
        ++tx->commits_ro;
        CM::onCommit(tx);
        tx->userCallbacks.onCommit();
        return;
    }

    // note: we're using timestamps in the same manner as
    // OrecLazy... without the single-thread optimization

    // acquire locks
    FOREACH (WriteSet, i, tx->writes) {
        // get orec, read its version#
        orec_t* o = get_orec(i->address);
        uintptr_t ivt = o->v.all;

        // lock all orecs, unless already locked
        if (ivt <= tx->start_time) {
            // abort if cannot acquire
            if (!bcasptr(&o->v.all, ivt, tx->my_lock.all))
                _ITM_abortTransaction(TMConflict);
            // save old version to o->p, remember that we hold the lock
            o->p = ivt;
            tx->locks.insert(o);
        }
        // else if we don't hold the lock abort
        else if (ivt != tx->my_lock.all) {
            _ITM_abortTransaction(TMConflict);
        }
    }

    // validate
    FOREACH (OrecList, i, tx->r_orecs) {
        uintptr_t ivt = (*i)->v.all;
        // if unlocked and newer than start time, abort
        if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
            _ITM_abortTransaction(TMConflict);
    }

    tx->writes.redo();

    // increment the global timestamp, release locks
    uintptr_t end_time = 1 + faiptr(&timestamp.val);
    FOREACH (OrecList, i, tx->locks) {
        (*i)->v.all = end_time;
    }

    // clean up
    CM::onCommit(tx);
    tx->undo_log.reset();               // ITM _ITM_LOG support
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->locks.reset();
    tx->allocator.onTxCommit();
    ++tx->commits_rw;
    tx->userCallbacks.onCommit();
}

namespace {
/**
 *  OrecLazy read
 */
  struct Read {
      void* operator()(void** addr, TX* tx, uintptr_t) const {
          // get the orec addr
          orec_t* o = get_orec(addr);
          while (true) {
              // read the location
              void* tmp = *addr;
              CFENCE;
              // read orec
              id_version_t ivt;
              ivt.all = o->v.all;

              // common case: new read to uncontended location
              if (ivt.all <= tx->start_time) {
                  tx->r_orecs.insert(o);
                  return tmp;
              }

              // if lock held, spin and retry
              if (ivt.fields.lock) {
                  spin64();
                  continue;
              }

              // scale timestamp if ivt is too new
              uintptr_t newts = timestamp.val;
              validate(tx);
              tx->start_time = newts;
          }
      }

      void preRead(TX*) {}
      void postRead(TX*) {}
  };
}

void*
alg_tm_read(void** addr) {
    return Lazy<void*, Read>::RSTM::Read(addr);
}

void
alg_tm_write(void** addr, void* val) {
    Lazy<void*, Read>::RSTM::Write(addr, val);
}

bool
alg_tm_is_irrevocable(TX*) {
    assert(false && "Unimplemented");
    return false;
}

void
alg_tm_become_irrevocable(_ITM_transactionState) {
    assert(false && "Unimplemented");
    return;
}

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
    TYPE CALLING_CONVENTION __attribute__((weak))                       \
    SYMBOL(TYPE* addr) {                                                \
        return Lazy<TYPE, Read>::ITM::Read(addr);                       \
    }

#define RSTM_LIBITM_WRITE(SYMBOL, CALLING_CONVENTION, TYPE)             \
    void CALLING_CONVENTION __attribute__((weak))                       \
    SYMBOL(TYPE* addr, TYPE val) {                                      \
        Lazy<TYPE, Read>::ITM::Write(addr, val);                        \
    }

#define RSTM_LIBITM_LOG(SYMBOL, CALLING_CONVENTION, TYPE)   \
    void CALLING_CONVENTION __attribute__((weak))           \
        SYMBOL(TYPE* addr) {                                \
        Lazy<TYPE, Read>::ITM::Log(addr);                   \
    }

#define RSTM_LIBITM_MEMCPY(SYMBOL, RTx, WTx)                        \
    void __attribute__((weak))                                      \
    SYMBOL(void* dest, const void* src, size_t n) {                 \
        Lazy<uint8_t, Read>::ITM::Memcpy<RTx, WTx>(dest, src, n);   \
    }

#define RSTM_LIBITM_MEMMOVE(SYMBOL, RTx, WTx)                       \
    void __attribute__((weak))                                      \
    SYMBOL(void* dest, const void* src, size_t n) {                 \
        Lazy<uint8_t, Read>::ITM::Memmove<RTx, WTx>(dest, src, n);  \
    }

#define RSTM_LIBITM_MEMSET(SYMBOL)                                  \
    void __attribute__((weak))                                      \
    SYMBOL(void* target, int src, size_t n) {                       \
        Lazy<uint8_t, Read>::ITM::Memset(target, src, n);           \
    }

#include "libitm-dtfns.def"

#undef RSTM_LIBITM_LOG
#undef RSTM_LIBITM_WRITE
#undef RSTM_LIBITM_READ
