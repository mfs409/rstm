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
 *  CohortsEager Implementation
 *
 *  Original cohorts algorithm
 */

#include <stdint.h>
#include <iostream>
#include <cassert>
#include <setjmp.h> // factor this out into the API?
#include "../common/platform.hpp"
#include "WriteSet.hpp"
#include "WBMMPolicy.hpp" // todo: remove this, use something simpler
#include "Macros.hpp"

// define atomic operations
#define CAS __sync_val_compare_and_swap
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch

namespace stm
{

  /**
   *  id_version_t uses the msb as the lock bit.  If the msb is zero, treat
   *  the word as a version number.  Otherwise, treat it as a struct with the
   *  lower 8 bits giving the ID of the lock-holding thread.
   */
  union id_version_t
  {
      struct
      {
          // ensure msb is lock bit regardless of platform
#if defined(STM_CPU_X86) /* little endian */
          uintptr_t id:(8*sizeof(uintptr_t))-1;
          uintptr_t lock:1;
#else /* big endian (probably SPARC) */
          uintptr_t lock:1;
          uintptr_t id:(8*sizeof(uintptr_t))-1;
#endif
      } fields;
      uintptr_t all; // read entire struct in a single load
  };

  /**
   * When we acquire an orec, we may ultimately need to reset it to its old
   * value (if we abort).  Saving the old value with the orec is an easy way to
   * support this need without having exta logging in the descriptor.
   */
  struct orec_t
  {
      volatile id_version_t v; // current version number or lockBit + ownerId
      volatile uintptr_t    p; // previous version number
  };

  typedef MiniVector<orec_t*>      OrecList;     // vector of orecs

  // Global variables for Cohorts
  volatile uint32_t locks[9] = {0};  // a big lock at locks[0], and
                                      // small locks from locks[1] to locks[8]
  volatile int32_t started = 0;    // number of tx started
  volatile int32_t cpending = 0;   // number of tx waiting to commit
  volatile int32_t committed = 0;  // number of tx committed
  volatile int32_t last_order = 0; // order of last tx in a cohort + 1
  volatile uint32_t gatekeeper = 0;// indicating whether tx can start

  pad_word_t last_complete = {0};

  /**
   *  This is the Orec Timestamp, the NOrec/TML seqlock, the CGL lock, and the
   *  RingSW ring index
   */
  pad_word_t timestamp = {0};

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

      OrecList       r_orecs;       // read set for orec STMs

      uintptr_t      ts_cache;      // last validation time
      intptr_t       order;         // for stms that order txns eagerly

      int aborts;

      scope_t* volatile scope;      // used to roll back; also flag for isTxnl

      WriteSet       writes;        // write set

      WBMMPolicy allocator;

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
  TX::TX() : nesting_depth(0), commits_ro(0), commits_rw(0), r_orecs(64), ts_cache(0), order(-1),
             aborts(0), scope(NULL), writes(64), allocator()
  {
      id = faiptr(&threadcount.val);
      threads[id] = this;
      allocator.setID(id-1);
  }

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
  const char* tm_getalgname() { return "CohortsEager"; }

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
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->allocator.onTxAbort();
      tx->nesting_depth = 0;
      scope_t* scope = tx->scope;
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

  static const uint32_t NUM_STRIPES   = 1048576;  // number of orecs

  /*** the set of orecs (locks) */
  orec_t orecs[NUM_STRIPES] = {{{{0}}}};

  /**
   *  Map addresses to orec table entries
   */
  TM_INLINE
  inline orec_t* get_orec(void* addr)
  {
      uintptr_t index = reinterpret_cast<uintptr_t>(addr);
      return &orecs[(index>>3) % NUM_STRIPES];
  }

  /**
   *  Validate a transaction by ensuring that its reads have not changed
   */
  NOINLINE
  void validate(TX* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // If orec changed , abort
          //
          // [mfs] norec recently switched to full validation, with a return
          //       val of true or false depending on whether or not to abort.
          //       Should evaluate if that is faster here.
          if (ivt > tx->ts_cache) {
              // increase total number of committed tx
              ADD(&committed, 1);
              // set self as completed
              last_complete.val = tx->order;
              // abort
              tm_abort(tx);
          }
      }
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

    S1:
      // wait until everyone is committed
      while (cpending != committed);

      // before tx begins, increase total number of tx
      ADD(&started, 1);

      // [NB] we must double check no one is ready to commit yet
      // and no one entered in place write phase(turbo mode)
      if (cpending > committed) {
          SUB(&started, 1);
          goto S1;
      }

      tx->allocator.onTxBegin();
      // get time of last finished txn
      tx->ts_cache = last_complete.val;
  }

  /**
   *  Commit a (possibly flat nested) transaction
   */
  void tm_end()
  {
      TX* tx = Self;
      if (--tx->nesting_depth)
          return;

      if (!tx->writes.size()) {
          // decrease total number of tx started
          SUB(&started, 1);

          // clean up
          tx->r_orecs.reset();
          tx->allocator.onTxCommit();
          ++tx->commits_ro;
          return;
      }

      // increase # of tx waiting to commit, and use it as the order
      tx->order = ADD(&cpending ,1);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // If I'm not the first one in a cohort to commit, validate read
      if (tx->order != last_order)
          validate(tx);

      foreach (WriteSet, i, tx->writes) {
          // get orec
          orec_t* o = get_orec(i->addr);
          // mark orec
          o->v.all = tx->order;
      }

      // Wait until all tx are ready to commit
      while (cpending < started);

      // do write back
      foreach (WriteSet, i, tx->writes)
          *i->addr = i->val;

      // update last_order
      last_order = started + 1;

      // mark self as done
      last_complete.val = tx->order;

      // increase total number of committed tx
      // [NB] atomic increment is faster here
      ADD(&committed, 1);
      // committed++;
      // WBR;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
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

      // log orec
      tx->r_orecs.insert(get_orec(addr));
      return *addr;
  }

  /**
   *  Simple buffered transactional write
   */
  void tm_write(void** addr, void* val)
  {
      TX* tx = Self;

      // record the new value in a redo log
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

} // namespace stm

