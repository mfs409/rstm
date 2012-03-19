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
 *  OrecALA Implementation
 *
 *    This is similar to the Detlefs algorithm for privatization-safe STM,
 *    TL2-IP, and [Marathe et al. ICPP 2008].  We use commit time ordering to
 *    ensure that there are no delayed cleanup problems, and we poll the
 *    timestamp variable to address doomed transactions.  By using TL2-style
 *    timestamps, we also achieve ALA publication safety
 */

#include <iostream>
#include <setjmp.h>
#include "MiniVector.hpp"
#include "metadata.hpp"
#include "WriteSet.hpp"
#include "WBMMPolicy.hpp"
#include "Macros.hpp"
#include "../common/locks.hpp"

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
      OrecList       r_orecs;       // read set for orec STMs
      OrecList       locks;         // list of all locks held by tx

      uintptr_t      start_time;    // start time of transaction
      uintptr_t      end_time;
      uintptr_t      ts_cache;

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
      allocator.setID(id-1);
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
  const char* tm_getalgname() { return "OrecELA"; }

  /**
   *  To initialize the thread's TM support, we need only ensure it has a
   *  descriptor.
   */
  void tm_thread_init()
  {
      // multiple inits from one thread do not cause trouble
      if (Self) return;

      // create a TX and save it in thread-local storage
      Self = new TX();
  }

  /**
   *  When a thread is done using the TM, we don't need to do anything
   *  special.
   */
  void tm_thread_shutdown() { }

  /*** The only metadata we need is a single global padded lock ***/
  pad_word_t timestamp = {0};
  pad_word_t last_complete = {0};

  /**
   *  OrecALA rollback:
   *
   *    This is a standard orec unwind function.  The only catch is that if a
   *    transaction aborted after incrementing the timestamp, it must wait its
   *    turn and then increment the trailing timestamp, to keep the two counters
   *    consistent.
   */
  scope_t* rollback(TX* tx)
  {
      ++tx->aborts;


      // release the locks and restore version numbers
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = (*i)->p;
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();

      CFENCE;

      // if we aborted after incrementing the timestamp, then we have to
      // participate in the global cleanup order to support our solution to
      // the deferred update half of the privatization problem.
      // NB:  Note that end_time is always zero for restarts and retrys
      if (tx->end_time != 0) {
          while (last_complete.val < (tx->end_time - 1))
              spin64();
          CFENCE;
          last_complete.val = tx->end_time;
      }
      CFENCE;
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

  /**
   *  OrecALA begin:
   *
   *    We need a starting point for the transaction.  If an in-flight
   *    transaction is committed, but still doing writeback, we can either start
   *    at the point where that transaction had not yet committed, or else we can
   *    wait for it to finish writeback.  In this code, we choose the former
   *    option.
   *
   *    NB: the latter option might be better, since there is no timestamp
   *        scaling
   */
  void tm_begin(scope_t* scope)
  {
      TX* tx = Self;
      if (++tx->nesting_depth > 1)
          return;
      tx->scope = scope;

      tx->allocator.onTxBegin();
      // Start after the last cleanup, instead of after the last commit, to
      // avoid spinning in begin()
      tx->start_time = last_complete.val;
      tx->ts_cache = tx->start_time;
      tx->end_time = 0;
  }

  void validate_commit(TX* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
              tm_abort(tx);
      }
  }

  /**
   *  OrecALA commit (read-only):
   *
   *    RO commit is trivial
   */
  void tm_end()
  {
      TX* tx = Self;
      if (--tx->nesting_depth)
          return;

      CFENCE;
      if (!tx->writes.size()) {
          tx->r_orecs.reset();
          tx->allocator.onTxCommit();
          ++tx->commits_ro;
          return;
      }

      // acquire locks
      foreach (WriteSet, i, tx->writes) {
          // get orec, read its version#
          orec_t* o = get_orec(i->addr);
          uintptr_t ivt = o->v.all;

          // if orec not locked, lock it and save old to orec.p
          if (ivt <= tx->start_time) {
              // abort if cannot acquire
              if (!bcasptr(&o->v.all, ivt, tx->my_lock.all))
                  tm_abort(tx);
              // save old version to o->p, remember that we hold the lock
              o->p = ivt;
              tx->locks.insert(o);
          }
          else if (ivt != tx->my_lock.all) {
              tm_abort(tx);
          }
      }
      CFENCE;

      // increment the global timestamp
      tx->end_time = 1 + faiptr(&timestamp.val);
      CFENCE;
      // skip validation if nobody committed since my last validation
      if (tx->end_time != (tx->ts_cache + 1)) {
          validate_commit(tx);
      }
      CFENCE;
      // run the redo log
      tx->writes.writeback();

      // release locks
      CFENCE;
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = tx->end_time;
      CFENCE;
      // now ensure that transactions depart from stm_end in the order that
      // they incremend the timestamp.  This avoids the "deferred update"
      // half of the privatization problem.
      while (last_complete.val != (tx->end_time - 1))
          spin64();
      last_complete.val = tx->end_time;

      // clean-up
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      tx->allocator.onTxCommit();
      ++tx->commits_rw;
  }

  /**
   *  OrecALA validation
   *
   *    an in-flight transaction must make sure it isn't suffering from the
   *    "doomed transaction" half of the privatization problem.  We can get that
   *    effect by calling this after every transactional read.
   */
  NOINLINE
  void privtest(TX* tx, uintptr_t ts)
  {
      // optimized validation since we don't hold any locks
      foreach (OrecList, i, tx->r_orecs) {
          // if orec unlocked and newer than start time, it changed, so abort.
          // if locked, it's not locked by me so abort
          if ((*i)->v.all > tx->start_time)
              tm_abort(tx);
      }

      // remember that we validated at this time
      tx->ts_cache = ts;
  }

  /**
   *  OrecALA read (read-only transaction)
   *
   *    Standard tl2-style read, but then we poll for potential privatization
   *    conflicts
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

      // read the location, log the orec
      void* tmp = *addr;
      orec_t* o = get_orec(addr);
      tx->r_orecs.insert(o);
      CFENCE;

      // make sure this location isn't locked or too new
      if (o->v.all > tx->start_time)
          tm_abort(tx);

      // privatization safety: poll the timestamp, maybe validate
      uintptr_t ts = timestamp.val;
      if (ts != tx->ts_cache)
          privtest(tx, ts);
      // return the value we read
      return tmp;
  }

  /**
   *  OrecALA write (read-only context)
   *
   *    Buffer the write, and switch to a writing context.
   */
  void tm_write(void** addr, void* val)
  {
      TX* tx = Self;
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

}
