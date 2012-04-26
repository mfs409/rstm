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
 *  CTokenTurbo Implementation
 *
 *    This code is like CToken, except we aggressively check if a thread is the
 *    'oldest', and if it is, we switch to an irrevocable 'turbo' mode with
 *    in-place writes and no validation.
 */

#include <stdint.h>
#include <iostream>
#include <cassert>
#include <unistd.h>
#include "byte-logging.hpp"
#include "tmabi-weak.hpp"
#include "foreach.hpp"
#include "inst.hpp"
#include "WBMMPolicy.hpp"
#include "tx.hpp"
#include "adaptivity.hpp"
#include "tm_alloc.hpp"
#include "libitm.h"

using namespace stm;

static pad_word_t timestamp = {0};
static pad_word_t last_complete = {0};

/**
 *  For querying to get the current algorithm name
 */
const char* alg_tm_getalgname() {
    return "CToken";
}

/**
 *  CToken unwinder:
 */
void alg_tm_rollback(TX* tx)
{
    ++tx->aborts;
    tx->undo_log.undo();
    tx->r_orecs.reset();
    tx->writes.reset();
    // NB: we can't reset pointers here, because if the transaction
    //     performed some writes, then it has an order.  If it has an
    //     order, but restarts and is read-only, then it still must call
    //     commit_rw to finish in-order
    tx->allocator.onTxAbort();
    tx->userCallbacks.onRollback();
}

/**
 *  CToken validation
 */
static NOINLINE void validate(TX* tx, uintptr_t finish_cache)
{
    FOREACH (OrecList, i, tx->r_orecs) {
        // read this orec
        uintptr_t ivt = (*i)->v.all;
        // if it has a timestamp of ts_cache or greater, abort
        if (ivt > tx->ts_cache)
            _ITM_abortTransaction(TMConflict);
    }
    // now update the finish_cache to remember that at this time, we were
    // still valid
    tx->ts_cache = finish_cache;
}

/**
 *  CToken begin: only called for outermost transactions.
 */
uint32_t alg_tm_begin(uint32_t, TX* tx, uint32_t extra)
{
    tx->allocator.onTxBegin();

    // get time of last finished txn
    tx->ts_cache = last_complete.val;
    return extra | a_runInstrumentedCode;
}

/**
 *  CToken commit (read-only):
 */
void alg_tm_end()
{
    TX* tx = Self;
    if (--tx->nesting_depth)
        return;

    // NB: we can have no writes but still have an order, if we aborted
    //     after our first write.  In that case, we need to participate in
    //     ordered commit, and can't take the RO fastpath
    if (tx->order == -1) {
        tx->undo_log.reset();
        tx->r_orecs.reset();
        tx->allocator.onTxCommit();
        ++tx->commits_ro;
        tx->userCallbacks.onCommit();
        return;
    }

    // we need to transition to fast here, but not till our turn
    while (last_complete.val != ((uintptr_t)tx->order - 1)) { }

    // validate
    if (last_complete.val > tx->ts_cache)
        validate(tx, last_complete.val);

    // writeback
    if (tx->writes.size() != 0) {
        // mark every location in the write set, and perform write-back
        FOREACH (WriteSet, i, tx->writes) {
            orec_t* o = get_orec(i->address);
            o->v.all = tx->order;
            CFENCE; // WBW
            i->value.writeTo(i->address);
        }
    }

    CFENCE; // wbw between writeback and last_complete.val update
    last_complete.val = tx->order;

    // set status to committed...
    tx->order = -1;

    // commit all frees, reset all lists
    tx->undo_log.reset();
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->allocator.onTxCommit();
    ++tx->commits_rw;
    tx->userCallbacks.onCommit();
}

namespace {
/**
 *  CToken read (writing transaction)
 */
  struct Read {
      void* operator()(void** addr, TX* tx, uintptr_t) const {
          void* tmp = *addr;
          CFENCE; // RBR between dereference and orec check

          // get the orec addr, read the orec's version#
          orec_t* o = get_orec(addr);
          uintptr_t ivt = o->v.all;
          // abort if this changed since the last time I saw someone finish
          if (ivt > tx->ts_cache)
              _ITM_abortTransaction(TMConflict);

          // log orec
          tx->r_orecs.insert(o);

          // validate, and if we have writes, then maybe switch to fast mode
          if (last_complete.val > tx->ts_cache)
              validate(tx, last_complete.val);
          return tmp;
      }
  };

  struct Write {
      void operator()(void** addr, void* val, TX* tx, uintptr_t mask) const {
          if (tx->order == -1) {
              // we don't have any writes yet, so we need to get an order here
              tx->order = 1 + faiptr(&timestamp.val);
          }

          // record the new value in a redo log
          tx->writes.insert(addr, val, mask);
      }
  };

  template <typename T>
  struct Inst {
      typedef GenericInst<T, true, Word,
                          CheckWritesetForReadOnly,
                          NoFilter, Read, NullType,
                          NoFilter, Write, NullType> RSTM;

      typedef GenericInst<T, false, LoggingWordType,
                          CheckWritesetForReadOnly,
                          FullFilter, Read, NullType,
                          FullFilter, Write, NullType> ITM;
  };
}

void* alg_tm_read(void** addr) {
    return Inst<void*>::RSTM::Read(addr);
}

void alg_tm_write(void** addr, void* val) {
    Inst<void*>::RSTM::Write(addr, val);
}

bool alg_tm_is_irrevocable(TX* tx) {
    assert(false && "Unimplemented");
    return false;
}

void alg_tm_become_irrevocable(_ITM_transactionState) {
    assert(false && "Unimplemented");
    return;
}

/**
 *  Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(CToken)

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

#define RSTM_LIBITM_LOG(SYMBOL, CALLING_CONVENTION, TYPE)   \
    void CALLING_CONVENTION __attribute__((weak))           \
        SYMBOL(TYPE* addr) {                                \
        Inst<TYPE>::ITM::Log(addr);                         \
    }

#include "libitm-dtfns.def"

#undef RSTM_LIBITM_LOG
#undef RSTM_LIBITM_WRITE
#undef RSTM_LIBITM_READ
