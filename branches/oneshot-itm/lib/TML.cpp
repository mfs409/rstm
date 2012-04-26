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
 *  TML Implementation
 *
 *    This STM was published by Dalessandro et al. at EuroPar 2010.  The
 *    algorithm allows multiple readers or a single irrevocable writer.  The
 *    semantics are at least as strong as ALA.
 *
 *    NB: now that we dropped the inlined-tml instrumentation hack, we should
 *        probably add ro/rw functions
 */

#include <cassert>
#include "tmabi-weak.hpp"
#include "tx.hpp"
#include "inst3.hpp"
#include "adaptivity.hpp"
#include "tm_alloc.hpp"
#include "libitm.h"

using namespace stm;

/*** The only metadata we need is a single global padded lock ***/
static pad_word_t timestamp = {0};

/**
 *  For querying to get the current algorithm name
 */
const char* alg_tm_getalgname() {
    return "TML";
}

/**
 *  Abort and roll back the transaction (e.g., on conflict).
 */
void alg_tm_rollback(TX* tx) {
    ++tx->aborts;
    tx->allocator.onTxAbort();
    tx->userCallbacks.onRollback();
}

/**
 *  TML requires this to be called after every read
 */
inline static void afterread_TML(TX* tx) {
    CFENCE;
    if (__builtin_expect(timestamp.val != tx->start_time, false))
        _ITM_abortTransaction(TMConflict);
}

/**
 *  TML requires this to be called before every write
 */
inline static void beforewrite_TML(TX* tx) {
    // acquire the lock, abort on failure
    if (!bcasptr(&timestamp.val, tx->start_time, tx->start_time + 1))
        _ITM_abortTransaction(TMConflict);
    ++tx->start_time;
    tx->turbo = true;
}

/**
 *  Start a (possibly flat nested) transaction. Only called for outer
 *  transactions.
 */
uint32_t alg_tm_begin(uint32_t, TX* tx, uint32_t extra) {
    // Sample the sequence lock until it is even (unheld)
    //
    // [mfs] Consider using NOrec trick to just decrease and start
    // running... we'll die more often, but with less overhead for readers...
    while ((tx->start_time = timestamp.val) & 1) { }

    // notify the allocator
    tx->allocator.onTxBegin();
    return extra | a_runInstrumentedCode;
}

/**
 *  Commit a (possibly flat nested) transaction
 */
void alg_tm_end() {
    TX* tx = Self;
    if (--tx->nesting_depth)
        return;

    // writing context: release lock, free memory, remember commit
    if (tx->turbo) {
        ++timestamp.val;
        tx->turbo = false;
        tx->allocator.onTxCommit();
        ++tx->commits_rw;
    }
    // reading context: just remember the commit
    else {
        tx->allocator.onTxCommit();
        ++tx->commits_ro;
    }

    tx->userCallbacks.onCommit();
}

namespace {
  struct Read {
      void* operator()(void** addr, TX* tx, uintptr_t mask) const {
          void* val = *addr;
          afterread_TML(tx);
          return val;
      }
  };

  template <class WordType>
  struct Write {
      void operator()(void** addr, void* val, TX* tx, uintptr_t mask) const {
          beforewrite_TML(tx);
          WordType::Write(addr, val, mask);
      }
  };

  template <typename T>
  struct Inst {
      typedef GenericInst<T, true, NullType,
                          NoReadOnly,
                          TurboFilter<NoFilter>, Read, NullType,
                          TurboFilter<NoFilter>, Write<Word>, NullType> RSTM;
      typedef GenericInst<T, false, NullType,
                          NoReadOnly,
                          TurboFilter<FullFilter>, Read, NullType,
                          TurboFilter<NoFilter>, Write<LoggingWordType>, NullType> ITM;
  };
}

/**
 *  Transactional read
 */
void* alg_tm_read(void** addr) {
    return Inst<void*>::RSTM::Read(addr);
}

/**
 *  Transactional write
 */
void alg_tm_write(void** addr, void* val) {
    Inst<void*>::RSTM::Write(addr, val);
}

bool alg_tm_is_irrevocable(TX* tx) {
    return (tx->turbo);
}

void alg_tm_become_irrevocable(_ITM_transactionState) {
    assert(false && "Unimplemented");
    return;
}

/**
 *  Register the TM for adaptivity
 */
REGISTER_TM_FOR_ADAPTIVITY(TML)

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
    SYMBOL(TYPE* addr, TYPE val) {                                      \
        Inst<TYPE>::ITM::Write(addr, val);                              \
    }

#include "libitm-dtfns.def"

#undef RSTM_LIBITM_WRITE
#undef RSTM_LIBITM_READ
