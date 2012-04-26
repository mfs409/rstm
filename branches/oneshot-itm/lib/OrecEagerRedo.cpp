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
#include "byte-logging.hpp"
#include "tmabi-weak.hpp"
#include "foreach.hpp"
#include "MiniVector.hpp"
#include "metadata.hpp"
#include "WBMMPolicy.hpp"
#include "tx.hpp"
#include "adaptivity.hpp"
#include "tm_alloc.hpp"
#include "libitm.h"
#include "inst3.hpp"

using namespace stm;

/**
 *  For querying to get the current algorithm name
 */
const char* alg_tm_getalgname() {
    return "OrecEagerRedo";
}

/**
 *  OrecEagerRedo unwinder:
 *
 *    To unwind, we must release locks, but we don't have an undo log to run.
 */
void alg_tm_rollback(TX* tx)
{
    ++tx->aborts;

    // release the locks and restore version numbers
    FOREACH (OrecList, i, tx->locks) {
        (*i)->v.all = (*i)->p;
    }

    // undo memory operations, reset lists
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->locks.reset();
    tx->allocator.onTxAbort();
}

/*** The only metadata we need is a single global padded lock ***/
static pad_word_t timestamp = {0};

/**
 *  OrecEagerRedo begin:
 *
 *    Standard begin: just get a start time, only called for outermost
 *    transactions.
 */
uint32_t alg_tm_begin(uint32_t, TX* tx, uint32_t extra)
{
    tx->allocator.onTxBegin();
    tx->start_time = timestamp.val;
    return extra | a_runInstrumentedCode;
}

/**
 *  OrecEagerRedo validation
 *
 *    validate the read set by making sure that all orecs that we've read have
 *    timestamps older than our start time, unless we locked those orecs.
 */
static NOINLINE void validate(TX* tx)
{
    FOREACH (OrecList, i, tx->r_orecs) {
        // read this orec
        uintptr_t ivt = (*i)->v.all;
        // if unlocked and newer than start time, abort
        if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
            _ITM_abortTransaction(TMConflict);
    }
}

/**
 *  OrecEagerRedo commit (read-only):
 *
 *    Standard commit: we hold no locks, and we're valid, so just clean up
 */
void alg_tm_end()
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
    FOREACH (OrecList, i, tx->r_orecs) {
        // read this orec
        uintptr_t ivt = (*i)->v.all;
        // if unlocked and newer than start time, abort
        if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
            _ITM_abortTransaction(TMConflict);
    }

    // run the redo log
    tx->writes.redo();

    // we're a writer, so increment the global timestamp
    uintptr_t end_time = 1 + faiptr(&timestamp.val);

    // release locks
    FOREACH (OrecList, i, tx->locks) {
        (*i)->v.all = end_time;
    }

    // clean up
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->locks.reset();
    tx->allocator.onTxCommit();
    ++tx->commits_rw;
}

namespace {
  /**
   *  OrecEagerRedo read
   *
   *  OrecEagerRedo is a bit of an odd beast in that it acquires eager
   *  ownership of to-write locations, but also buffers the writes that it will
   *  perform. This doesn't fit well into our generic read/write
   *  instrumentation scheme. In particular, the rest of the lazy-versioning
   *  STMs perform RAW checks before executing any algorithm-specific read
   *  instrumentation, while OrecEagerRedo is written instead to perform RAW
   *  checks only when it detects that an orec is owned by itself.
   */
  template <typename WordType>
  struct Read {
      /**
       *  The RAW lookup should use the standard RAW instrumentation, in order to
       *  correctly deal with byte-logging configurations. If we are byte-logging,
       *  and we only get a partial hit, then we need to extract the missing
       *  bytes from the word in memory, but we don't need to acquire any sort
       *  of additional ownership of the orec (we already own the orec for
       *  writing). This "PlainReader" policy is used to instantiate the Raw
       *  class, which Raw uses to get the word from memory. (note that this
       *  code is also used when we have an orec collision on something we own)
       */
      struct PlainReader {
          void* operator()(void** address, TX*, uintptr_t) const {
              return *address;
          }
      };

      void* operator()(void** addr, TX* tx, uintptr_t mask)  const{
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
                  // check the log for a RAW hazard, the Raw class handles the
                  // possibility of partial RAW hits due to byte-logging
                  Raw<PlainReader, WordType> raw(tx);
                  raw(addr, tmp, mask);
                  return tmp;
              }

              // abort if locked by other
              if (ivt.fields.lock)
                  _ITM_abortTransaction(TMConflict);

              // scale timestamp if ivt is too new
              uintptr_t newts = timestamp.val;
              validate(tx);
              tx->start_time = newts;
          }
      }
  };

  /**
   *  OrecEagerRedo write
   */
  struct Write {
      void operator()(void** addr, void* val, TX* tx, uintptr_t mask) const {
          // add to redo log
          tx->writes.insert(addr, val, mask);

          // get the orec addr
          orec_t* o = get_orec(addr);
          while (true) {
              // read the orec version number
              id_version_t ivt;
              ivt.all = o->v.all;

              // common case: uncontended location... lock it
              if (ivt.all <= tx->start_time) {
                  if (!bcasptr(&o->v.all, ivt.all, tx->my_lock.all))
                      _ITM_abortTransaction(TMConflict);

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
                  _ITM_abortTransaction(TMConflict);

              // unlocked but too new... scale forward and try again
              uintptr_t newts = timestamp.val;
              validate(tx);
              tx->start_time = newts;
          }
      }
  };

  template <typename T>
  struct Inst {
      typedef GenericInst<T, true, NullType,
                          NoReadOnly,
                          NoFilter, Read<Word>, NullType,
                          NoFilter, Write, NullType> RSTM;
      typedef GenericInst<T, false, NullType,
                          NoReadOnly,
                          FullFilter, Read<LoggingWordType>, NullType,
                          FullFilter, Write, NullType> ITM;
  };
}

void* alg_tm_read(void** addr) {
    return Inst<void*>::RSTM::Read(addr);
}

void alg_tm_write(void** addr, void* val) {
    Inst<void*>::RSTM::Write(addr, val);
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
REGISTER_TM_FOR_ADAPTIVITY(OrecEagerRedo)

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
        return Inst<TYPE>::ITM::Read(addr);                             \
    }

#define RSTM_LIBITM_WRITE(SYMBOL, CALLING_CONVENTION, TYPE)             \
    void CALLING_CONVENTION __attribute__((weak))                       \
        SYMBOL(TYPE* addr, TYPE val) {                                  \
        Inst<TYPE>::ITM::Write(addr, val);                              \
    }

#include "libitm-dtfns.def"

#undef RSTM_LIBITM_WRITE
#undef RSTM_LIBITM_READ
