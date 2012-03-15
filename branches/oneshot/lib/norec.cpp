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


/**
 *  Warning: this has the entire old NOrec implementation (multiple templated
 *  versions) in an #ifdef 0 block.  The current live code is present far
 *  below, starting at #else.  Ultimately this will need to be templatized to
 *  provide the desired NOrec variants
 */

#if 0
#include "../cm.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

// Don't just import everything from stm. This helps us find bugs.
using stm::TxThread;
using stm::timestamp;
using stm::WriteSetEntry;
using stm::ValueList;
using stm::ValueListEntry;

namespace {

  const uintptr_t VALIDATION_FAILED = 1;
  NOINLINE uintptr_t validate(TxThread*);
  bool irrevoc(TxThread*);
  void onSwitchTo();

  template <class CM>
  struct NOrec_Generic
  {
      static TM_FASTCALL bool begin(TxThread*);
      static TM_FASTCALL void commit(TxThread*);
      static TM_FASTCALL void commit_ro(TxThread*);
      static TM_FASTCALL void commit_rw(TxThread*);
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,,));
      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static void initialize(int id, const char* name);
  };

  uintptr_t
  validate(TxThread* tx)
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

  bool
  irrevoc(TxThread* tx)
  {
      while (!bcasptr(&timestamp.val, tx->start_time, tx->start_time + 1))
          if ((tx->start_time = validate(tx)) == VALIDATION_FAILED)
              return false;

      // redo writes
      tx->writes.writeback();

      // Release the sequence lock, then clean up
      CFENCE;
      timestamp.val = tx->start_time + 2;
      tx->vlist.reset();
      tx->writes.reset();
      return true;
  }

  void
  onSwitchTo() {
      // We just need to be sure that the timestamp is not odd, or else we will
      // block.  For safety, increment the timestamp to make it even, in the event
      // that it is odd.
      if (timestamp.val & 1)
          ++timestamp.val;
  }


  template <typename CM>
  void
  NOrec_Generic<CM>::initialize(int id, const char* name)
  {
      // set the name
      stm::stms[id].name = name;

      // set the pointers
      stm::stms[id].begin     = NOrec_Generic<CM>::begin;
      stm::stms[id].commit    = NOrec_Generic<CM>::commit_ro;
      stm::stms[id].read      = NOrec_Generic<CM>::read_ro;
      stm::stms[id].write     = NOrec_Generic<CM>::write_ro;
      stm::stms[id].irrevoc   = irrevoc;
      stm::stms[id].switcher  = onSwitchTo;
      stm::stms[id].privatization_safe = true;
      stm::stms[id].rollback  = NOrec_Generic<CM>::rollback;
  }

  template <class CM>
  bool
  NOrec_Generic<CM>::begin(TxThread* tx)
  {
      // Originally, NOrec required us to wait until the timestamp is odd
      // before we start.  However, we can round down if odd, in which case
      // we don't need control flow here.

      // Sample the sequence lock, if it is even decrement by 1
      tx->start_time = timestamp.val & ~(1L);

      // notify the allocator
      tx->allocator.onTxBegin();

      // notify CM
      CM::onBegin(tx);

      return false;
  }

  template <class CM>
  void
  NOrec_Generic<CM>::commit(TxThread* tx)
  {
      // From a valid state, the transaction increments the seqlock.  Then it
      // does writeback and increments the seqlock again

      // read-only is trivially successful at last read
      if (!tx->writes.size()) {
          CM::onCommit(tx);
          tx->vlist.reset();
          OnReadOnlyCommit(tx);
          return;
      }

      // get the lock and validate (use RingSTM obstruction-free technique)
      while (!bcasptr(&timestamp.val, tx->start_time, tx->start_time + 1))
          if ((tx->start_time = validate(tx)) == VALIDATION_FAILED)
              tx->tmabort(tx);

      tx->writes.writeback();

      // Release the sequence lock, then clean up
      CFENCE;
      timestamp.val = tx->start_time + 2;
      CM::onCommit(tx);
      tx->vlist.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx);
  }

  template <class CM>
  void
  NOrec_Generic<CM>::commit_ro(TxThread* tx)
  {
      // Since all reads were consistent, and no writes were done, the read-only
      // NOrec transaction just resets itself and is done.
      CM::onCommit(tx);
      tx->vlist.reset();
      OnReadOnlyCommit(tx);
  }

  template <class CM>
  void
  NOrec_Generic<CM>::commit_rw(TxThread* tx)
  {
      // From a valid state, the transaction increments the seqlock.  Then it does
      // writeback and increments the seqlock again

      // get the lock and validate (use RingSTM obstruction-free technique)
      while (!bcasptr(&timestamp.val, tx->start_time, tx->start_time + 1))
          if ((tx->start_time = validate(tx)) == VALIDATION_FAILED)
              tx->tmabort(tx);

      tx->writes.writeback();

      // Release the sequence lock, then clean up
      CFENCE;
      timestamp.val = tx->start_time + 2;

      // notify CM
      CM::onCommit(tx);

      tx->vlist.reset();
      tx->writes.reset();

      // This switches the thread back to RO mode.
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  template <class CM>
  void*
  NOrec_Generic<CM>::read_ro(STM_READ_SIG(tx,addr,mask))
  {
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
              tx->tmabort(tx);
          tmp = *addr;
          CFENCE;
      }

      // log the address and value, uses the macro to deal with
      // STM_PROTECT_STACK
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  template <class CM>
  void*
  NOrec_Generic<CM>::read_rw(STM_READ_SIG(tx,addr,mask))
  {
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // Use the code from the read-only read barrier. This is complicated by
      // the fact that, when we are byte logging, we may have successfully read
      // some bytes from the write log (if we read them all then we wouldn't
      // make it here). In this case, we need to log the mask for the rest of the
      // bytes that we "actually" need, which is computed as bytes in mask but
      // not in log.mask. This is only correct because we know that a failed
      // find also reset the log.mask to 0 (that's part of the find interface).
      void* val = read_ro(tx, addr STM_MASK(mask & ~log.mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  template <class CM>
  void
  NOrec_Generic<CM>::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // buffer the write, and switch to a writing context
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  template <class CM>
  void
  NOrec_Generic<CM>::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // just buffer the write
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  template <class CM>
  stm::scope_t*
  NOrec_Generic<CM>::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      stm::PreRollback(tx);

      // notify CM
      CM::onAbort(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      tx->vlist.reset();
      tx->writes.reset();
      return stm::PostRollback(tx, read_ro, write_ro, commit_ro);
  }
} // (anonymous namespace)

// Register NOrec initializer functions. Do this as declaratively as
// possible. Remember that they need to be in the stm:: namespace.
#define FOREACH_NOREC(MACRO)                    \
    MACRO(NOrec, HyperAggressiveCM)             \
    MACRO(NOrecHour, HourglassCM)               \
    MACRO(NOrecBackoff, BackoffCM)              \
    MACRO(NOrecHB, HourglassBackoffCM)

#define INIT_NOREC(ID, CM)                      \
    template <>                                 \
    void initTM<ID>() {                         \
        NOrec_Generic<CM>::initialize(ID, #ID);     \
    }

namespace stm {
  FOREACH_NOREC(INIT_NOREC)
}
#undef FOREACH_NOREC
#undef INIT_NOREC


#else

#include <stdint.h>
#include <iostream>
#include <cassert>
#include <setjmp.h> // factor this out into the API?
#include "../common/platform.hpp"
#include "ValueList.hpp"
#include "WriteSet.hpp"
#include "WBMMPolicy.hpp"
#include "Macros.hpp"

namespace stm
{
  /**
   *  Store per-thread metadata.  There isn't much for CGL...
   */
  struct TX
  {
      /*** for flat nesting ***/
      int nesting_depth;

      /*** unique id for this thread ***/
      int id;

      /*** number of RO commits ***/
      int commits_ro;

      /*** number of RW commits ***/
      int commits_rw;

      int aborts;

      scope_t* volatile scope;      // used to roll back; also flag for isTxnl

      WriteSet       writes;        // write set
      ValueList      vlist;         // NOrec read log
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
  TX::TX() : nesting_depth(0), commits_ro(0), commits_rw(0), aborts(0),
             scope(NULL), writes(64), vlist(64), allocator(), start_time(0)
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
  const char* tm_getalgname() { return "NOrec"; }

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
  stm::scope_t* rollback(TX* tx)
  {
      ++tx->aborts;
      tx->vlist.reset();
      tx->writes.reset();
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

      // Originally, NOrec required us to wait until the timestamp is odd
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
  void tm_end()
  {
      TX* tx = Self;
      if (--tx->nesting_depth)
          return;

      // read-only is trivially successful at last read
      if (!tx->writes.size()) {
          tx->vlist.reset();
          tx->allocator.onTxCommit();
          ++tx->commits_ro;
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

  /**
   * [mfs] We should factor the rest of this into a common.cpp file of some
   *       sort
   */

  /**
   * We use malloc a couple of times here, and this makes it a bit easier
   */
  template <typename T>
  inline T* typed_malloc(size_t N)
  {
      return static_cast<T*>(malloc(sizeof(T) * N));
  }

  /**
   * This doubles the size of the index. This *does not* do anything as
   * far as actually doing memory allocation. Callers should delete[] the
   * index table, increment the table size, and then reallocate it.
   */
  NOINLINE
  size_t WriteSet::doubleIndexLength()
  {
      assert(shift != 0 &&
             "ERROR: the writeset doesn't support an index this large");
      shift   -= 1;
      ilength  = 1 << (8 * sizeof(uint32_t) - shift);
      return ilength;
  }

  /***  Writeset constructor.  Note that the version must start at 1. */
  WriteSet::WriteSet(const size_t initial_capacity)
      : index(NULL), shift(8 * sizeof(uint32_t)), ilength(0),
        version(1), list(NULL), capacity(initial_capacity), lsize(0)
  {
      // Find a good index length for the initial capacity of the list.
      while (ilength < 3 * initial_capacity)
          doubleIndexLength();

      index = new index_t[ilength];
      list  = typed_malloc<WriteSetEntry>(capacity);
  }

  /***  Writeset destructor */
  WriteSet::~WriteSet()
  {
      delete[] index;
      free(list);
  }

  /***  Rebuild the writeset */
  NOINLINE
  void WriteSet::rebuild()
  {
      assert(version != 0 && "ERROR: the version should *never* be 0");

      // extend the index
      delete[] index;
      index = new index_t[doubleIndexLength()];

      for (size_t i = 0; i < lsize; ++i) {
          const WriteSetEntry& l = list[i];
          size_t h = hash(l.addr);

          // search for the next available slot
          while (index[h].version == version)
              h = (h + 1) % ilength;

          index[h].address = l.addr;
          index[h].version = version;
          index[h].index   = i;
      }
  }

  /***  Resize the writeset */
  NOINLINE
  void WriteSet::resize()
  {
      WriteSetEntry* temp  = list;
      capacity     *= 2;
      list          = typed_malloc<WriteSetEntry>(capacity);
      memcpy(list, temp, sizeof(WriteSetEntry) * lsize);
      free(temp);
  }

  /***  Another writeset reset function that we don't want inlined */
  NOINLINE
  void WriteSet::reset_internal()
  {
      memset(index, 0, sizeof(index_t) * ilength);
      version = 1;
  }

} // (anonymous namespace)

#endif

