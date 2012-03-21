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
#include <setjmp.h>
#include "MiniVector.hpp"
#include "metadata.hpp"
#include "WriteSet.hpp"
#include "WBMMPolicy.hpp"
#include "Macros.hpp"

namespace stm
{
  typedef MiniVector<orec_t*>      OrecList;     // vector of orecs

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

      id_version_t   my_lock;       // lock word for orec STMs

      int aborts;

      scope_t* volatile scope;      // used to roll back; also flag for isTxnl

      WriteSet       writes;        // write set
      WBMMPolicy     allocator;     // buffer malloc/free
      uintptr_t      start_time;    // start time of transaction
      OrecList       r_orecs;       // read set for orec STMs
      OrecList       locks;         // list of all locks held by tx

      /*** constructor ***/
      TX();
  };

  /**
   *  Simple constructor for TX: zero all fields, get an ID
   */
  TX::TX() : nesting_depth(0), commits_ro(0), commits_rw(0), r_orecs(64), locks(64),
             aborts(0), scope(NULL), writes(64), allocator()
  {
      id = faiptr(&threadcount.val);
      threads[id] = this;
      allocator.setID(id);
      // set up my lock word
      my_lock.fields.lock = 1;
      my_lock.fields.id = id;
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
  const char* tm_getalgname() { return "OrecEagerRedo"; }

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
   *  OrecEagerRedo unwinder:
   *
   *    To unwind, we must release locks, but we don't have an undo log to run.
   */
  scope_t* rollback(TX* tx)
  {
      ++tx->aborts;

      // release the locks and restore version numbers
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = (*i)->p;

      // undo memory operations, reset lists
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
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

  /*** The only metadata we need is a single global padded lock ***/
  pad_word_t timestamp = {0};

  /**
   *  OrecEagerRedo begin:
   *
   *    Standard begin: just get a start time
   */
  void tm_begin(scope_t* scope)
  {
      TX* tx = Self;
      if (++tx->nesting_depth > 1)
          return;

      tx->scope = scope;
      tx->allocator.onTxBegin();
      tx->start_time = timestamp.val;
  }

  /**
   *  OrecEagerRedo validation
   *
   *    validate the read set by making sure that all orecs that we've read have
   *    timestamps older than our start time, unless we locked those orecs.
   */
  NOINLINE
  void validate(TX* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
              tm_abort(tx);
      }
  }

  /**
   *  OrecEagerRedo commit (read-only):
   *
   *    Standard commit: we hold no locks, and we're valid, so just clean up
   */
  void tm_end()
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
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
              tm_abort(tx);
      }

      // run the redo log
      tx->writes.writeback();

      // we're a writer, so increment the global timestamp
      uintptr_t end_time = 1 + faiptr(&timestamp.val);

      // release locks
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = end_time;

      // clean up
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      tx->allocator.onTxCommit();
      ++tx->commits_rw;
  }

  /**
   *  OrecEagerRedo read
   */
  void* tm_read(void** addr)
  {
      TX* tx = Self;

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
              // check the log for a RAW hazard, we expect to miss
              WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
              bool found = tx->writes.find(log);
              if (found)
                  return log.val;
              return *addr;
          }

          // abort if locked by other
          if (ivt.fields.lock)
              tm_abort(tx);

          // scale timestamp if ivt is too new
          uintptr_t newts = timestamp.val;
          validate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrecEagerRedo write
   */
  void tm_write(void** addr, void* val)
  {
      TX* tx = Self;

      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec version number
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: uncontended location... lock it
          if (ivt.all <= tx->start_time) {
              if (!bcasptr(&o->v.all, ivt.all, tx->my_lock.all))
                  tm_abort(tx);

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
              tm_abort(tx);

          // unlocked but too new... scale forward and try again
          uintptr_t newts = timestamp.val;
          validate(tx);
          tx->start_time = newts;
      }
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
