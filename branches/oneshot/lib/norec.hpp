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
 *  NOrec Implementation
 *
 *    This STM was published by Dalessandro et al. at PPoPP 2010.  The
 *    algorithm uses a single sequence lock, along with value-based validation,
 *    for concurrency control.  This variant offers semantics at least as
 *    strong as Asymmetric Lock Atomicity (ALA).
 */

#include <stdint.h>
#include <iostream>
#include <cassert>
#include <setjmp.h> // factor this out into the API?
#include "platform.hpp"
#include "ValueList.hpp"
#include "WriteSet.hpp"
#include "WBMMPolicy.hpp"
#include "Macros.hpp"
#include "tx.hpp"

namespace stm
{

  // for CM
  pad_word_t fcm_timestamp = {0};
  pad_word_t epochs[MAX_THREADS] = {{0}};

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

  const uintptr_t VALIDATION_FAILED = 1;

  /**
   *  Validate a transaction by ensuring that its reads have not changed
   */
  NOINLINE
  uintptr_t validate(TX* tx)
  {
      while (true) {
          // read the lock until it is even
          uintptr_t s = timestamp.val;
          if ((s & 1) == 1)
              continue;

          // check the read set
          CFENCE;
          // don't branch in the loop---consider it backoff if we fail
          // validation early
          bool valid = true;
          foreach (ValueList, i, tx->vlist)
              valid &= STM_LOG_VALUE_IS_VALID(i, tx);

          if (!valid)
              return VALIDATION_FAILED;

          // restart if timestamp changed during read set iteration
          CFENCE;
          if (timestamp.val == s)
              return s;
      }
  }

  /**
   *  Abort and roll back the transaction (e.g., on conflict).
   */
  template <class CM>
  __attribute__((always_inline))
  stm::scope_t* rollback_generic(TX* tx)
  {
      ++tx->aborts;
      tx->vlist.reset();
      tx->writes.reset();
      tx->allocator.onTxAbort();
      tx->nesting_depth = 0;
      CM::onAbort(tx);
      stm::scope_t* scope = tx->scope;
      tx->scope = NULL;
      return scope;
  }

  /**
   *  Forward-declare rollback so that we can call it from tm_abort
   */
  scope_t* rollback(TX* tx);

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
   *  Start a (possibly flat nested) transaction.
   *
   *  [mfs] Eventually need to inline setjmp into this method
   */
  template <class CM>
  __attribute__((always_inline))
  void tm_begin_generic(scope_t* scope)
  {
      TX* tx = Self;
      if (++tx->nesting_depth > 1)
          return;

      CM::onBegin(tx);

      tx->scope = scope;

      // Originally, NOrec required us to wait until the timestamp is even
      // before we start.  However, we can round down if odd, in which case
      // we don't need control flow here.

      // Sample the sequence lock, if it is even decrement by 1
      tx->start_time = timestamp.val & ~(1L);

      // notify the allocator
      tx->allocator.onTxBegin();
  }

  /**
   *  Commit a (possibly flat nested) transaction
   */
  template <class CM>
  __attribute__((always_inline))
  void tm_end_generic()
  {
      TX* tx = Self;
      if (--tx->nesting_depth)
          return;

      // read-only is trivially successful at last read
      if (!tx->writes.size()) {
          tx->vlist.reset();
          tx->allocator.onTxCommit();
          ++tx->commits_ro;
          CM::onCommit(tx);
          return;
      }

      // From a valid state, the transaction increments the seqlock.  Then it
      // does writeback and increments the seqlock again

      // get the lock and validate (use RingSTM obstruction-free technique)
      while (!bcasptr(&timestamp.val, tx->start_time, tx->start_time + 1))
          if ((tx->start_time = validate(tx)) == VALIDATION_FAILED)
              tm_abort(tx);

      tx->writes.writeback();

      // Release the sequence lock, then clean up
      CFENCE;
      timestamp.val = tx->start_time + 2;
      CM::onCommit(tx);
      tx->vlist.reset();
      tx->writes.reset();
      tx->allocator.onTxCommit();
      ++tx->commits_rw;
  }

  /**
   *  Transactional read
   */
  void* tm_read(void** addr)
  {
      TX* tx = Self;

      if (tx->writes.size()) {
          // check the log for a RAW hazard, we expect to miss
          WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
          bool found = tx->writes.find(log);
          if (found)
              return log.val;
      }

      // A read is valid iff it occurs during a period where the seqlock does
      // not change and is even.  This code also polls for new changes that
      // might necessitate a validation.

      // read the location to a temp
      void* tmp = *addr;
      CFENCE;

      // if the timestamp has changed since the last read, we must validate and
      // restart this read
      while (tx->start_time != timestamp.val) {
          if ((tx->start_time = validate(tx)) == VALIDATION_FAILED)
              tm_abort(tx);
          tmp = *addr;
          CFENCE;
      }

      // log the address and value, uses the macro to deal with
      // STM_PROTECT_STACK
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;

  }

  /**
   *  Simple buffered transactional write
   */
  void tm_write(void** addr, void* val)
  {
      TX* tx = Self;
      // just buffer the write
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
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
