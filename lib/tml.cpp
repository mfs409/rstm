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
   *  Simple constructor for TX: zero all fields, get an ID
   */
  TX::TX() : nesting_depth(0), tmlHasLock(false), commits_ro(0), commits_rw(0), aborts(0),
             scope(NULL), allocator(), start_time(0)
  {
      id = faiptr(&threadcount.val);
      threads[id] = this;
      allocator.setID(id);
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

} // namespace stm
