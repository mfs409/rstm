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
 *  Similiar to Cohorts, except that if I'm the last one in the cohort, I
 *  go to turbo mode, do in place read and write, and do turbo commit.
 */

#if 0
#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

// define atomic operations
#define CAS __sync_val_compare_and_swap
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch

using stm::TxThread;
using stm::last_complete;
using stm::timestamp;
using stm::timestamp_max;
using stm::WriteSet;
using stm::OrecList;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::orec_t;
using stm::get_orec;

using stm::started;
using stm::cpending;
using stm::committed;
using stm::last_order;

volatile uint32_t inplace = 0;
/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  struct CohortsEager {
      static TM_FASTCALL bool begin(TxThread*);
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_turbo(STM_READ_SIG(,,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_turbo(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void commit_ro(TxThread* tx);
      static TM_FASTCALL void commit_rw(TxThread* tx);
      static TM_FASTCALL void commit_turbo(TxThread* tx);

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE void validate(TxThread* tx);
  };

  /**
   *  CohortsEager begin:
   *  CohortsEager has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishes their
   *  commits.
   */
  bool
  CohortsEager::begin(TxThread* tx)
  {
    S1:
      // wait until everyone is committed
      while (cpending != committed);

      // before tx begins, increase total number of tx
      ADD(&started, 1);

      // [NB] we must double check no one is ready to commit yet
      // and no one entered in place write phase(turbo mode)
      if (cpending > committed || inplace == 1){
          SUB(&started, 1);
          goto S1;
      }

      tx->allocator.onTxBegin();
      // get time of last finished txn
      tx->ts_cache = last_complete.val;

      return true;
  }

  /**
   *  CohortsEager commit (read-only):
   */
  void
  CohortsEager::commit_ro(TxThread* tx)
  {
      // decrease total number of tx started
      SUB(&started, 1);

      // clean up
      tx->r_orecs.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  CohortsEager commit (in place write commit): no validation, no write back
   *  no other thread touches cpending
   */
  void
  CohortsEager::commit_turbo(TxThread* tx)
  {
      // increase # of tx waiting to commit, and use it as the order
      cpending ++;

      // clean up
      tx->r_orecs.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);

      // wait for my turn, in this case, cpending is my order
      while (last_complete.val != (uintptr_t)(cpending - 1));

      // reset in place write flag
      inplace = 0;

      // mark self as done
      last_complete.val = cpending;

      // increase # of committed
      committed ++;
      WBR;
  }

  /**
   *  CohortsEager commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given at the beginning of commit.
   */
  void
  CohortsEager::commit_rw(TxThread* tx)
  {
      // increase # of tx waiting to commit, and use it as the order
      tx->order = ADD(&cpending ,1);

      // Wait for my turn
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // Wait until all tx are ready to commit
      while (cpending < started);

      // If in place write occurred, all tx validate reads
      // Otherwise, only first one skips validation
      if (inplace == 1 || tx->order != last_order)
          validate(tx);

      foreach (WriteSet, i, tx->writes) {
          // get orec
          orec_t* o = get_orec(i->addr);
          // mark orec
          o->v.all = tx->order;
          // do write back
          *i->addr = i->val;
      }

      // increase total number of committed tx
      // [NB] Using atomic instruction might be faster
      // ADD(&committed, 1);
      committed ++;
      WBR;

      // update last_order
      last_order = started + 1;

      // mark self as done
      last_complete.val = tx->order;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsEager read_turbo
   */
  void*
  CohortsEager::read_turbo(STM_READ_SIG(tx,addr,))
  {
      return *addr;
  }

  /**
   *  CohortsEager read (read-only transaction)
   */
  void*
  CohortsEager::read_ro(STM_READ_SIG(tx,addr,))
  {
      // log orec
      tx->r_orecs.insert(get_orec(addr));
      return *addr;
  }

  /**
   *  CohortsEager read (writing transaction)
   */
  void*
  CohortsEager::read_rw(STM_READ_SIG(tx,addr,mask))
  {
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // log orec
      tx->r_orecs.insert(get_orec(addr));

      void* tmp = *addr;
      REDO_RAW_CLEANUP(tmp, found, log, mask);
      return tmp;
  }

  /**
   *  CohortsEager write (read-only context): for first write
   */
  void
  CohortsEager::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // If everyone else is ready to commit, do in place write
      if (cpending + 1 == started) {
          // set up flag indicating in place write starts
          // [NB]When testing on MacOS, better use CAS
          inplace = 1;
          WBR;
          // double check is necessary
          if (cpending + 1 == started) {
              // mark orec
              orec_t* o = get_orec(addr);
              o->v.all = started;
              // in place write
              *addr = val;
              // go turbo mode
              OnFirstWrite(tx, read_turbo, write_turbo, commit_turbo);
              return;
          }
          // reset flag
          inplace = 0;
      }
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  CohortsEager write (in place write)
   */
  void
  CohortsEager::write_turbo(STM_WRITE_SIG(tx,addr,val,mask))
  {
      orec_t* o = get_orec(addr);
      o->v.all = started; // mark orec
      *addr = val; // in place write
  }

  /**
   *  CohortsEager write (writing context)
   */
  void
  CohortsEager::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsEager unwinder:
   */
  stm::scope_t*
  CohortsEager::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();

      return PostRollback(tx);
  }

  /**
   *  CohortsEager in-flight irrevocability:
   */
  bool
  CohortsEager::irrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsEager Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsEager validation for commit: check that all reads are valid
   */
  void
  CohortsEager::validate(TxThread* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // If orec changed , abort
          if (ivt > tx->ts_cache) {
              // increase total number of committed tx
              // ADD(&committed, 1);
              committed ++;
              WBR;
              // set self as completed
              last_complete.val = tx->order;
              // abort
              tx->tmabort(tx);
          }
      }
  }

  /**
   *  Switch to CohortsEager:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   */
  void
  CohortsEager::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = 0;
  }
}

namespace stm {
  /**
   *  CohortsEager initialization
   */
  template<>
  void initTM<CohortsEager>()
  {
      // set the name
      stms[CohortsEager].name      = "CohortsEager";
      // set the pointers
      stms[CohortsEager].begin     = ::CohortsEager::begin;
      stms[CohortsEager].commit    = ::CohortsEager::commit_ro;
      stms[CohortsEager].read      = ::CohortsEager::read_ro;
      stms[CohortsEager].write     = ::CohortsEager::write_ro;
      stms[CohortsEager].rollback  = ::CohortsEager::rollback;
      stms[CohortsEager].irrevoc   = ::CohortsEager::irrevoc;
      stms[CohortsEager].switcher  = ::CohortsEager::onSwitchTo;
      stms[CohortsEager].privatization_safe = true;
  }
}

#else

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
  volatile uint32_t inplace = 0;

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

      bool turbo;

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
  TX::TX() : nesting_depth(0), commits_ro(0), commits_rw(0), r_orecs(64), ts_cache(0), order(-1), turbo(false),
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
          if (ivt > tx->ts_cache) {
              // increase total number of committed tx
              // ADD(&committed, 1);
              committed++;
              WBR;
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
      if (cpending > committed || inplace == 1){
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

      if (tx->turbo) {
          // increase # of tx waiting to commit, and use it as the order
          cpending ++;

          // clean up
          tx->r_orecs.reset();
          tx->allocator.onTxCommit();
          ++tx->commits_rw;

          // wait for my turn, in this case, cpending is my order
          while (last_complete.val != (uintptr_t)(cpending - 1));

          // reset in place write flag
          inplace = 0;

          // mark self as done
          last_complete.val = cpending;

          // increase # of committed
          committed ++;
          WBR;
          tx->turbo = false;
          return;
      }

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

      // Wait until all tx are ready to commit
      while (cpending < started);

      // If in place write occurred, all tx validate reads
      // Otherwise, only first one skips validation
      if (inplace == 1 || tx->order != last_order)
          validate(tx);

      foreach (WriteSet, i, tx->writes) {
          // get orec
          orec_t* o = get_orec(i->addr);
          // mark orec
          o->v.all = tx->order;
          // do write back
          *i->addr = i->val;
      }

      // increase total number of committed tx
      // [NB] Using atomic instruction might be faster
      // ADD(&committed, 1);
      committed ++;
      WBR;

      // update last_order
      last_order = started + 1;

      // mark self as done
      last_complete.val = tx->order;

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

      if (tx->turbo) {
          return *addr;
      }

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

      if (tx->turbo) {
          orec_t* o = get_orec(addr);
          o->v.all = started; // mark orec
          *addr = val; // in place write
          return;
      }

      if (!tx->writes.size() && 0) {
          // If everyone else is ready to commit, do in place write
          if (cpending + 1 == started) {
              // set up flag indicating in place write starts
              // [NB] When testing on MacOS, better use CAS
              inplace = 1;
              WBR;
              // double check is necessary
              if (cpending + 1 == started) {
                  // mark orec
                  orec_t* o = get_orec(addr);
                  o->v.all = started;
                  // in place write
                  *addr = val;
                  // go turbo mode
                  tx->turbo = true;
                  return;
              }
              // reset flag
              inplace = 0;
          }
          tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
          return;
      }

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
#endif
