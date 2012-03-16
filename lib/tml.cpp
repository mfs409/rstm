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

/**
 *  Warning: this has the entire old TML implementation (multiple templated
 *  versions) in an #ifdef 0 block.  The current live code is present far
 *  below, starting at #else.
 */

#if 0

#include "../profiling.hpp"
#include "algs.hpp"
#include "tml_inline.hpp"
#include <stm/UndoLog.hpp> // STM_DO_MASKED_WRITE

using stm::TxThread;
using stm::timestamp;
using stm::Trigger;
using stm::UNRECOVERABLE;


/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.  Note that with TML, we don't expect the reads and
 *  writes to be called, because we expect the instrumentation to be inlined
 *  via the dispatch mechanism.  However, we must provide the code to handle
 *  the uncommon case.
 */
namespace {
  struct TML {
      static TM_FASTCALL bool begin(TxThread*);
      static TM_FASTCALL void* read(STM_READ_SIG(,,));
      static TM_FASTCALL void write(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void commit(TxThread*);

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
  };

  /**
   *  TML begin:
   */
  bool
  TML::begin(TxThread* tx)
  {
      int counter = 0;
      // Sample the sequence lock until it is even (unheld)
      while ((tx->start_time = timestamp.val) & 1) {
          spin64();
          counter += 64;
      }

      // notify the allocator
      tx->begin_wait = counter;
      tx->allocator.onTxBegin();
      return false;
  }

  /**
   *  TML commit:
   */
  void
  TML::commit(TxThread* tx)
  {
      // writing context: release lock, free memory, remember commit
      if (tx->tmlHasLock) {
          ++timestamp.val;
          tx->tmlHasLock = false;
          OnReadWriteCommit(tx);
      }
      // reading context: just remember the commit
      else {
          OnReadOnlyCommit(tx);
      }
      Trigger::onCommitLock(tx);
  }

  /**
   *  TML read:
   *
   *    If we have the lock, we're irrevocable so just do a read.  Otherwise,
   *    after doing the read, make sure we are still valid.
   */
  void*
  TML::read(STM_READ_SIG(tx,addr,))
  {
      void* val = *addr;
      if (tx->tmlHasLock)
          return val;
      // NB:  afterread_tml includes a CFENCE
      afterread_TML(tx);
      return val;
  }

  /**
   *  TML write:
   *
   *    If we have the lock, do an in-place write and return.  Otherwise, we
   *    need to become irrevocable first, then do the write.
   */
  void
  TML::write(STM_WRITE_SIG(tx,addr,val,mask))
  {
      if (tx->tmlHasLock) {
          STM_DO_MASKED_WRITE(addr, val, mask);
          return;
      }
      // NB:  beforewrite_tml includes a fence via CAS
      beforewrite_TML(tx);
      STM_DO_MASKED_WRITE(addr, val, mask);
  }

  /**
   *  TML unwinder
   *
   *    NB: This should not be called from a writing context!  That means
   *        calling restart() under TML with writes is not allowed, but we
   *        don't currently enforce.
   *
   *    NB: don't need to worry about exception object since anyone rolling
   *        back must be read-only, and thus the logs have no writes to
   *        exception objects pending.
   */
  stm::scope_t*
  TML::rollback(STM_ROLLBACK_SIG(tx,,))
  {
      PreRollback(tx);
      return PostRollback(tx);
  }

  /**
   *  TML in-flight irrevocability:
   *
   *    We have a custom path for going irrevocable in TML, so this code should
   *    never be called.
   */
  bool
  TML::irrevoc(TxThread*)
  {
      UNRECOVERABLE("IRREVOC_TML SHOULD NEVER BE CALLED");
      return false;
  }

  /**
   *  Switch to TML:
   *
   *    We just need to be sure that the timestamp is not odd, or else we will
   *    block.  For safety, increment the timestamp to make it even, in the
   *    event that it is odd.
   */
  void
  TML::onSwitchTo()
  {
      if (timestamp.val & 1)
          ++timestamp.val;
  }
} // (anonymous namespace)

namespace stm {
  template<>
  void initTM<TML>()
  {
      // set the name
      stms[TML].name      = "TML";

      // set the pointers
      stms[TML].begin     = ::TML::begin;
      stms[TML].commit    = ::TML::commit;
      stms[TML].read      = ::TML::read;
      stms[TML].write     = ::TML::write;
      stms[TML].rollback  = ::TML::rollback;
      stms[TML].irrevoc   = ::TML::irrevoc;
      stms[TML].switcher  = ::TML::onSwitchTo;
      stms[TML].privatization_safe = true;
  }
}
#else

#include <stdint.h>
#include <iostream>
#include <cassert>
#include <setjmp.h> // factor this out into the API?
#include "../common/platform.hpp"
#include "WBMMPolicy.hpp"

namespace stm
{
  /**
   *  Store per-thread metadata.  There isn't much for CGL...
   */
  struct TX
  {
      /*** for flat nesting ***/
      int nesting_depth;

      bool tmlHasLock;    // is tml thread holding the lock

      /*** unique id for this thread ***/
      int id;

      /*** number of RO commits ***/
      int commits_ro;

      /*** number of RW commits ***/
      int commits_rw;

      int aborts;

      scope_t* volatile scope;      // used to roll back; also flag for isTxnl

      WBMMPolicy     allocator;     // buffer malloc/free
      uintptr_t      start_time;    // start time of transaction

      /*** constructor ***/
      TX();
  };

  /**
   *  [mfs] We should factor the next three declarations into some sort of
   *        common cpp file
   */

  /**
   *  Array of all threads
   */
  TX* threads[MAX_THREADS];

  /**
   *  Thread-local pointer to self
   */
  __thread TX* Self = NULL;

  /*** Count of all threads ***/
  pad_word_t threadcount = {0};

  /**
   *  Simple constructor for TX: zero all fields, get an ID
   */
  TX::TX() : nesting_depth(0), tmlHasLock(false), commits_ro(0), commits_rw(0), aborts(0),
             scope(NULL), allocator(), start_time(0)
  {
      id = faiptr(&threadcount.val);
      threads[id] = this;
      allocator.setID(id-1);
  }

  /*** The only metadata we need is a single global padded lock ***/
  pad_word_t timestamp = {0};

  /**
   *  No system initialization is required, since the timestamp is already 0
   */
  void tm_sys_init() { }

  /**
   *  When the transactional system gets shut down, we call this to dump
   *  stats for all threads
   */
  void tm_sys_shutdown()
  {
      static volatile unsigned int mtx = 0;
      // while (!bcas32(&mtx, 0u, 1u)) { }
      for (uint32_t i = 0; i < threadcount.val; i++) {
          std::cout << "Thread: "       << threads[i]->id
                    << "; RO Commits: " << threads[i]->commits_ro
                    << "; RW Commits: " << threads[i]->commits_rw
                    << "; Aborts: "     << threads[i]->aborts
                    << std::endl;
      }
      CFENCE;
      mtx = 0;
  }

  /**
   *  For querying to get the current algorithm name
   */
  const char* tm_getalgname() { return "TML"; }

  /**
   *  To initialize the thread's TM support, we need only ensure it has a
   *  descriptor.
   */
  void tm_thread_init()
  {
      // multiple inits from one thread do not cause trouble
      if (Self) return;

      // create a TxThread and save it in thread-local storage
      Self = new TX();
  }

  /**
   *  When a thread is done using the TM, we don't need to do anything
   *  special.
   */
  void tm_thread_shutdown() { }

  /**
   *  Abort and roll back the transaction (e.g., on conflict).
   */
  stm::scope_t* rollback(TX* tx)
  {
      ++tx->aborts;
      tx->allocator.onTxAbort();
      tx->nesting_depth = 0;
      stm::scope_t* scope = tx->scope;
      tx->scope = NULL;
      return scope;
  }

  /**
   *  The default mechanism that libstm uses for an abort. An API environment
   *  may also provide its own abort mechanism (see itm2stm for an example of
   *  how the itm shim does this).
   *
   *  This is ugly because rollback has a configuration-dependent signature.
   */
  NOINLINE
  NORETURN
  void tm_abort(TX* tx)
  {
      jmp_buf* scope = (jmp_buf*)rollback(tx);
      // need to null out the scope
      longjmp(*scope, 1);
  }

  /**
   *  TML requires this to be called after every read
   */
  inline void afterread_TML(TX* tx)
  {
      CFENCE;
      if (__builtin_expect(timestamp.val != tx->start_time, false))
          tm_abort(tx);
  }

  /**
   *  TML requires this to be called before every write
   */
  inline void beforewrite_TML(TX* tx) {
      // acquire the lock, abort on failure
      if (!bcasptr(&timestamp.val, tx->start_time, tx->start_time + 1))
          tm_abort(tx);
      ++tx->start_time;
      tx->tmlHasLock = true;
  }

  /**
   *  Start a (possibly flat nested) transaction.
   *
   *  [mfs] Eventually need to inline setjmp into this method
   */
  void tm_begin(scope_t* scope)
  {
      TX* tx = Self;
      if (++tx->nesting_depth > 1)
          return;

      tx->scope = scope;

      // Sample the sequence lock until it is even (unheld)
      //
      // [mfs] Consider using NOrec trick to just decrease and start
      // running... we'll die more often, but with less overhead for readers...
      while ((tx->start_time = timestamp.val) & 1) { }

      // notify the allocator
      tx->allocator.onTxBegin();
  }

  /**
   *  Commit a (possibly flat nested) transaction
   */
  void tm_end()
  {
      TX* tx = Self;
      if (--tx->nesting_depth)
          return;

      // writing context: release lock, free memory, remember commit
      if (tx->tmlHasLock) {
          ++timestamp.val;
          tx->tmlHasLock = false;
          tx->allocator.onTxCommit();
          ++tx->commits_rw;
      }
      // reading context: just remember the commit
      else {
          tx->allocator.onTxCommit();
          ++tx->commits_ro;
      }
  }

  /**
   *  Transactional read
   */
  void* tm_read(void** addr)
  {
      TX* tx = Self;
      void* val = *addr;
      if (tx->tmlHasLock)
          return val;
      // NB:  afterread_tml includes a CFENCE
      afterread_TML(tx);
      return val;
  }

  /**
   *  Simple buffered transactional write
   */
  void tm_write(void** addr, void* val)
  {
      TX* tx = Self;
      if (tx->tmlHasLock) {
          *addr = val;
          return;
      }
      // NB:  beforewrite_tml includes a fence via CAS
      beforewrite_TML(tx);
      *addr = val;
  }

  /**
   *  get a chunk of memory that will be automatically reclaimed if the caller
   *  is a transaction that ultimately aborts
   */
  void* tm_alloc(size_t size) { return Self->allocator.txAlloc(size); }

  /**
   *  Free some memory.  If the caller is a transaction that ultimately aborts,
   *  the free will not happen.  If the caller is a transaction that commits,
   *  the free will happen at commit time.
   */
  void tm_free(void* p) { Self->allocator.txFree(p); }

} // (anonymous namespace)

#endif
