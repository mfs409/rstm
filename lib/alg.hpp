/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <stdint.h>
#include <iostream>
#include <cstdlib>
#include "tx.hpp"
#include "platform.hpp"
#include "locks.hpp"
#include "metadata.hpp"
#include "adaptivity.hpp"
#include "NOrec.hpp"
#include "cm.hpp"

using namespace stm;

namespace cgl
{
  /**
   * The only metadata we need is a single global padded lock
   */
  extern pad_word_t timestamp;

  /**
   *  For querying to get the current algorithm name
   */
  extern const char* tm_getalgname();

  /**
   *  Start a transaction: if we're already in a tx, bump the nesting
   *  counter.  Otherwise, grab the lock.  Note that we have a null parameter
   *  so that the signature is identical to all other STMs (prereq for
   *  adaptivity)
   */
  extern void tm_begin(void*);
   

  /**
   *  End a transaction: decrease the nesting level, then perhaps release the
   *  lock and increment the count of commits.
   */
  extern void tm_end();

  /**
   *  In CGL, malloc doesn't need any special care
   */
  extern void* tm_alloc(size_t s);

  /**
   *  In CGL, free doesn't need any special care
   */
  extern void tm_free(void* p);

  /**
   *  CGL read
   */
  TM_FASTCALL
  extern void* tm_read(void** addr);

  /**
   *  CGL write
   */
  TM_FASTCALL
  extern void tm_write(void** addr, void* val);

  extern scope_t* rollback(TX* tx);

}

namespace cohorts
{
  // Global variables for Cohorts
  extern volatile uint32_t locks[9];  // a big lock at locks[0], and
                                      // small locks from locks[1] to locks[8]
  extern volatile int32_t started;    // number of tx started
  extern volatile int32_t cpending;   // number of tx waiting to commit
  extern volatile int32_t committed;  // number of tx committed
  extern volatile int32_t last_order; // order of last tx in a cohort + 1
  extern volatile uint32_t gatekeeper;// indicating whether tx can start

  extern pad_word_t last_complete;

  /**
   *  This is the Orec Timestamp, the NOrec/TML seqlock, the CGL lock, and the
   *  RingSW ring index
   */
  extern pad_word_t timestamp;

  /**
   *  For querying to get the current algorithm name
   */
  extern const char* tm_getalgname();

  /**
   *  Abort and roll back the transaction (e.g., on conflict).
   */
  extern scope_t* rollback(TX* tx);

  /**
   *  Validate a transaction by ensuring that its reads have not changed
   */
  NOINLINE
  extern void validate(TX* tx);

  /**
   *  Start a (possibly flat nested) transaction.
   *
   *  [mfs] Eventually need to inline setjmp into this method
   */
  extern void tm_begin(scope_t* scope);

  /**
   *  Commit a (possibly flat nested) transaction
   */
  extern void tm_end();

  /**
   *  Transactional read
   */
  TM_FASTCALL
  extern void* tm_read(void** addr);

  /**
   *  Simple buffered transactional write
   */
  TM_FASTCALL
  extern void tm_write(void** addr, void* val);

  /**
   *  get a chunk of memory that will be automatically reclaimed if the caller
   *  is a transaction that ultimately aborts
   */
  extern void* tm_alloc(size_t size);

  /**
   *  Free some memory.  If the caller is a transaction that ultimately aborts,
   *  the free will not happen.  If the caller is a transaction that commits,
   *  the free will happen at commit time.
   */
  extern void tm_free(void* p);

} // namespace cohorts

/**
 * Instantiate rollback with the appropriate CM for this TM algorithm
 */
template scope_t* norec_generic::rollback_generic<HyperAggressiveCM>(TX*);

/**
 * Instantiate tm_begin with the appropriate CM for this TM algorithm
 */
template void norec_generic::tm_begin_generic<HyperAggressiveCM>(scope_t*);

/**
 * Instantiate tm_end with the appropriate CM for this TM algorithm
 */
template void norec_generic::tm_end_generic<HyperAggressiveCM>();

namespace norec
{

  /**
   * Create aliases to the norec_generic functions or instantiations that we
   * shall use for NOrec
   */
  extern scope_t* rollback(TX* tx) __attribute__((weak, alias("_ZN13norec_generic16rollback_genericIN3stm17HyperAggressiveCMEEEPvPNS1_2TXE")));

  extern void tm_begin(scope_t *) __attribute__((weak, alias("_ZN13norec_generic16tm_begin_genericIN3stm17HyperAggressiveCMEEEvPv")));
  extern void tm_end() __attribute__((weak, alias("_ZN13norec_generic14tm_end_genericIN3stm17HyperAggressiveCMEEEvv")));
  TM_FASTCALL
  extern void* tm_read(void**) __attribute__((weak, alias("_ZN13norec_generic7tm_readEPPv")));
  TM_FASTCALL
  extern void tm_write(void**, void*) __attribute__((weak, alias("_ZN13norec_generic8tm_writeEPPvS0_")));
  extern void* tm_alloc(size_t) __attribute__((weak, alias("_ZN13norec_generic8tm_allocEj")));
  extern void tm_free(void*) __attribute__((weak, alias("_ZN13norec_generic7tm_freeEPv")));

  /**
   *  For querying to get the current algorithm name
   */
  extern const char* tm_getalgname();

}

namespace tml
{
  /*** The only metadata we need is a single global padded lock ***/
  extern pad_word_t timestamp;

  /**
   *  For querying to get the current algorithm name
   */
  extern const char* tm_getalgname();

  /**
   *  Abort and roll back the transaction (e.g., on conflict).
   */
  extern scope_t* rollback(TX* tx);


  /**
   *  TML requires this to be called after every read
   */
  extern inline void afterread_TML(TX* tx);

  /**
   *  TML requires this to be called before every write
   */
  extern inline void beforewrite_TML(TX* tx);

  /**
   *  Start a (possibly flat nested) transaction.
   *
   *  [mfs] Eventually need to inline setjmp into this method
   */
  extern void tm_begin(scope_t* scope);

  /**
   *  Commit a (possibly flat nested) transaction
   */
  extern void tm_end();

  /**
   *  Transactional read
   */
  TM_FASTCALL
  extern void* tm_read(void** addr);

  /**
   *  Simple buffered transactional write
   */
  TM_FASTCALL
  extern void tm_write(void** addr, void* val);

  /**
   *  get a chunk of memory that will be automatically reclaimed if the caller
   *  is a transaction that ultimately aborts
   */
  extern void* tm_alloc(size_t size); 

  /**
   *  Free some memory.  If the caller is a transaction that ultimately aborts,
   *  the free will not happen.  If the caller is a transaction that commits,
   *  the free will happen at commit time.
   */
  extern void tm_free(void* p);

} // namespace tml

namespace cohortseager
{
  // Global variables for Cohorts
  extern volatile uint32_t locks[9];  // a big lock at locks[0], and
                                      // small locks from locks[1] to locks[8]
  extern volatile int32_t started;    // number of tx started
  extern volatile int32_t cpending;   // number of tx waiting to commit
  extern volatile int32_t committed;  // number of tx committed
  extern volatile int32_t last_order; // order of last tx in a cohort + 1
  extern volatile uint32_t gatekeeper;// indicating whether tx can start
  extern volatile uint32_t inplace;

  extern pad_word_t last_complete;

  /**
   *  This is the Orec Timestamp, the NOrec/TML seqlock, the CGL lock, and the
   *  RingSW ring index
   */
  extern pad_word_t timestamp;

  /**
   *  For querying to get the current algorithm name
   */
  extern const char* tm_getalgname();

  /**
   *  Abort and roll back the transaction (e.g., on conflict).
   */
  extern scope_t* rollback(TX* tx);

  /**
   *  Validate a transaction by ensuring that its reads have not changed
   */
  NOINLINE
  extern void validate(TX* tx);
  /**
   *  Start a (possibly flat nested) transaction.
   *
   *  [mfs] Eventually need to inline setjmp into this method
   */
  extern void tm_begin(scope_t* scope);
  /**
   *  Commit a (possibly flat nested) transaction
   */
  extern void tm_end();
  /**
   *  Transactional read
   */
  TM_FASTCALL
  extern void* tm_read(void** addr);
  /**
   *  Simple buffered transactional write
   */
  TM_FASTCALL
  extern void tm_write(void** addr, void* val);
  /**
   *  get a chunk of memory that will be automatically reclaimed if the caller
   *  is a transaction that ultimately aborts
   */
  extern void* tm_alloc(size_t size);

  /**
   *  Free some memory.  If the caller is a transaction that ultimately aborts,
   *  the free will not happen.  If the caller is a transaction that commits,
   *  the free will happen at commit time.
   */
  void tm_free(void* p);

} // namespace cohortseager

namespace ctokenturbo
{
  /*** The only metadata we need is a single global padded lock ***/
  extern pad_word_t timestamp;
  extern pad_word_t last_complete;

  /**
   *  For querying to get the current algorithm name
   */
  extern const char* tm_getalgname();

  /**
   *  CTokenTurbo unwinder:
   *
   *    NB: self-aborts in Turbo Mode are not supported.  We could add undo
   *        logging to address this, and add it in Pipeline too.
   */
  extern scope_t* rollback(TX* tx);

  /**
   *  CTokenTurbo validation
   */
  NOINLINE
  extern void validate(TX* tx, uintptr_t finish_cache);

  /**
   *  CTokenTurbo begin:
   */
  extern void tm_begin(scope_t* scope);

  /**
   *  CTokenTurbo commit (read-only):
   */
  extern void tm_end();
  /**
   *  CTokenTurbo read (read-only transaction)
   */
  extern void* read_ro(TX* tx, void** addr);

  /**
   *  CTokenTurbo read (writing transaction)
   */
  extern void* read_rw(TX* tx, void** addr);

  TM_FASTCALL
  extern void* tm_read(void** addr);
  /**
   *  CTokenTurbo write (read-only context)
   */
  TM_FASTCALL
  extern void tm_write(void** addr, void* val);

  /**
   *  get a chunk of memory that will be automatically reclaimed if the caller
   *  is a transaction that ultimately aborts
   */
  extern void* tm_alloc(size_t size); 

  /**
   *  Free some memory.  If the caller is a transaction that ultimately aborts,
   *  the free will not happen.  If the caller is a transaction that commits,
   *  the free will happen at commit time.
   */
  extern void tm_free(void* p); 

}

namespace ctoken
{

  extern pad_word_t timestamp;
  extern pad_word_t last_complete;

  /**
   *  For querying to get the current algorithm name
   */
  extern const char* tm_getalgname();

  /**
   *  CToken unwinder:
   */
  extern scope_t* rollback(TX* tx);

  /**
   *  CToken validation
   */
  NOINLINE
  extern void validate(TX* tx, uintptr_t finish_cache);
  /**
   *  CToken begin:
   */
  extern void tm_begin(scope_t* scope);

  /**
   *  CToken commit (read-only):
   */
  extern void tm_end();

  /**
   *  CToken read (writing transaction)
   */
  TM_FASTCALL
  extern void* tm_read(void** addr);
  /**
   *  CToken write (read-only context)
   */
  TM_FASTCALL
  extern void tm_write(void** addr, void* val);
  /**
   *  get a chunk of memory that will be automatically reclaimed if the caller
   *  is a transaction that ultimately aborts
   */
  extern void* tm_alloc(size_t size);

  /**
   *  Free some memory.  If the caller is a transaction that ultimately aborts,
   *  the free will not happen.  If the caller is a transaction that commits,
   *  the free will happen at commit time.
   */
  extern void tm_free(void* p);

}

