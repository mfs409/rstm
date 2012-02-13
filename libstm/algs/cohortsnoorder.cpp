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
 *  Cohortsnoorder Implementation
 *
 *  This algs is based on LLT, except that we add cohorts' properties.
 *  But unlike cohorts, we do not give orders at the beginning of any
 *  commits.
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

// define atomic operations
#define CAS __sync_val_compare_and_swap
#define ADD __sync_fetch_and_add
#define SUB __sync_fetch_and_sub

using stm::TxThread;
using stm::timestamp;
using stm::timestamp_max;
using stm::WriteSet;
using stm::OrecList;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::orec_t;
using stm::get_orec;

using stm::tx_total;
using stm::locks;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  struct Cohortsnoorder
  {
    static TM_FASTCALL bool begin(TxThread*);
    static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
    static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
    static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,,));
    static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,,));
    static TM_FASTCALL void commit_ro(TxThread*);
    static TM_FASTCALL void commit_rw(TxThread*);
    
    static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
    static bool irrevoc(TxThread*);
    static void onSwitchTo();
    static NOINLINE void validate(TxThread*);
    static NOINLINE void TxAbortWrapper(TxThread* tx);
    
  };

  /**
   *  Cohortsnoorder begin:
   *  At first, every tx can start, until one of the tx is ready to commit.
   *  Then no tx is allowed to start until all the transactions finishes their
   *  commits. 
   */
  bool
  Cohortsnoorder::begin(TxThread* tx)
  {
    // wait until we are allowed to start
    // when tx_total is even, we wait
    while (tx_total % 2 == 0){
      // unless tx_total is 0, which means all commits is done
      if (tx_total == 0)
	{
	  // set no validation, for big lock
	  locks[0] = 0;
	  
	  //now we can start again
	  CAS(&tx_total, 0, -1);
	}
      //check if an adaptivity action is underway
      if (TxThread::tmbegin != begin){
	tx->tmabort(tx);
      }
    }

    //before start, increase total number of tx in one cohort
    ADD(&tx_total, 2);

    // now start
    tx->allocator.onTxBegin();
    // get a start time
    tx->start_time = timestamp.val;
    
    return false;
  }

  /**
   *  Cohortsnoorder commit (read-only):
   *
   *    Decrease total number of tx, and commit.
   */
  void
  Cohortsnoorder::commit_ro(TxThread* tx)
  {
    // decrease total number of tx in a cohort
    SUB(&tx_total, 2);
    
    // read-only, so just reset lists
    tx->r_orecs.reset();
    OnReadOnlyCommit(tx);
  }

  /**
   *  Cohortsnoorder commit (writing context):
   *
   *    Change tx_total from odd to even when first commit occured in cohort.
   *    Forbid no validation read at the same time.
   *    Get all locks, validate, do writeback.  Use the counter to avoid some
   *    validations.
   */
  void
  Cohortsnoorder::commit_rw(TxThread* tx)
  {
    uint32_t tmp = tx_total;
    // if tx_total is odd, I'm the first to enter commit in a cohort
    if (tmp % 2 != 0)
      {
	// change it to even
	CAS(&tx_total, tmp, tmp+1);
	
	// set validation flag
	// we need validations in read from now on
	CAS(&locks[0], 0, 1); 
	
	// wait until all the small locks are unlocked
	for(uint32_t i = 1; i < 9; i++)
	  while(locks[i] != 0);
      }
    
    // acquire locks
    foreach (WriteSet, i, tx->writes) {
      // get orec, read its version#
      orec_t* o = get_orec(i->addr);
      uintptr_t ivt = o->v.all;
      
      // lock all orecs, unless already locked
      if (ivt <= tx->start_time) {
	// abort if cannot acquire
	if (!bcasptr(&o->v.all, ivt, tx->my_lock.all))
	  TxAbortWrapper(tx);
	// save old version to o->p, remember that we hold the lock
	o->p = ivt;
	tx->locks.insert(o);
      }
      // else if we don't hold the lock abort
      else if (ivt != tx->my_lock.all) {
	TxAbortWrapper(tx);
      }
    }
    
    // increment the global timestamp since we have writes
    uintptr_t end_time = 1 + faiptr(&timestamp.val);
    
    // skip validation if nobody else committed
    if (end_time != (tx->start_time + 1))
      validate(tx);
    
    // run the redo log
    tx->writes.writeback();
    
    // release locks
    CFENCE;
    foreach (OrecList, i, tx->locks)
      (*i)->v.all = end_time;
    
    // clean-up
    tx->r_orecs.reset();
    tx->writes.reset();
    tx->locks.reset();
    OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
    
    // decrease total number of committing tx
    SUB(&tx_total, 2);
  }
  
  /**
   *  Cohortsnoorder read (read-only transaction)
   *    We may don't need validations here if no one committed yet,
   *    but otherwise, we use "check twice" timestamps.
   */
  void*
  Cohortsnoorder::read_ro(STM_READ_SIG(tx,addr,))
  {
      // It is possible that no validation is needed
      if (tx_total % 2 != 0 && locks[0] == 0)
      {
	// mark my lock 1, means I'm doing no validation read_ro
	locks[tx->id] = 1;

	// no validation needed
	if (locks[0] == 0)
	  {
	    void* tmp1 = *addr;
	    // get the orec addr
	    orec_t* o = get_orec(addr);
	    // log orec
	    tx->r_orecs.insert(o);

	    // mark my lock 0, means I finished no validation read_ro
	    locks[tx->id] = 0;
	    return tmp1;
	  }
	else
	  // mark my lock 0, means I will do validation read_ro
	  locks[tx->id] = 0;
      }
      
      // get the orec addr
      orec_t* o = get_orec(addr);
      // read orec, then val, then orec
      uintptr_t ivt = o->v.all;
      CFENCE;
      void* tmp = *addr;
      CFENCE;
      uintptr_t ivt2 = o->v.all;
      // if orec never changed, and isn't too new, the read is valid
      if ((ivt <= tx->start_time) && (ivt == ivt2)) {
          // log orec, return the value
          tx->r_orecs.insert(o);
          return tmp;
      }
      // unreachable
      TxAbortWrapper(tx);
      return NULL;
  }

  /**
   *  Cohortsnoorder read (writing transaction)
   */
  void*
  Cohortsnoorder::read_rw(STM_READ_SIG(tx,addr,mask))
  {
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // It is possible that no validation is needed
      if (tx_total % 2 != 0 && locks[0] == 0)
      {
	// mark my lock 1, means I'm doing no validation read_ro
	locks[tx->id] = 1;

	// no validation needed
	if (locks[0] == 0)
	  {
	    void* tmp1 = *addr;
	    // get the orec addr
	    orec_t* o = get_orec(addr);
	    // log orec
	    tx->r_orecs.insert(o);

	    // fixup is here to minimize the postvalidation orec read latency
	    REDO_RAW_CLEANUP(tmp1, found, log, mask);
	    
	    // mark my lock 0, means I finished no validation read_ro
	    locks[tx->id] = 0;
	    return tmp1;
	  }
	else
	  // mark my lock 0, means I will do validation read_ro
	  locks[tx->id] = 0;
      }

      // get the orec addr
      orec_t* o = get_orec(addr);
      // read orec, then val, then orec
      uintptr_t ivt = o->v.all;
      CFENCE;
      void* tmp = *addr;
      CFENCE;
      uintptr_t ivt2 = o->v.all;

      // fixup is here to minimize the postvalidation orec read latency
      REDO_RAW_CLEANUP(tmp, found, log, mask);

      // if orec never changed, and isn't too new, the read is valid
      if ((ivt <= tx->start_time) && (ivt == ivt2)) {
          // log orec, return the value
          tx->r_orecs.insert(o);
          return tmp;
      }
      TxAbortWrapper(tx);
      // unreachable
      return NULL;
  }

  /**
   *  Cohortsnoorder write (read-only context)
   */
  void
  Cohortsnoorder::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  Cohortsnoorder write (writing context)
   */
  void
  Cohortsnoorder::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  Cohortsnoorder unwinder:
   */
  stm::scope_t*
  Cohortsnoorder::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // release the locks and restore version numbers
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = (*i)->p;

      // undo memory operations, reset lists
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      return PostRollback(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  Cohortsnoorder in-flight irrevocability:
   */
  bool
  Cohortsnoorder::irrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  Cohortsnoorder validation
   */
  void
  Cohortsnoorder::validate(TxThread* tx)
  {
      // validate
      foreach (OrecList, i, tx->r_orecs) {
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
	    TxAbortWrapper(tx);
      }
  }

  /**
   *   Cohorts Tx Abort Wrapper
   *   decrease total # in one cohort, and abort
   */
    void
    Cohortsnoorder::TxAbortWrapper(TxThread* tx)
    {
      // decrease total number of tx in one cohort
      SUB(&tx_total, 2);

      // abort
      tx->tmabort(tx);
    }
  
  /**
   *  Switch to Cohortsnoorder:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   */
  void
  Cohortsnoorder::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
  }
}

namespace stm {
  /**
   *  Cohortsnoorder initialization
   */
  template<>
  void initTM<Cohortsnoorder>()
  {
      // set the name
      stms[Cohortsnoorder].name      = "Cohortsnoorder";

      // set the pointers
      stms[Cohortsnoorder].begin     = ::Cohortsnoorder::begin;
      stms[Cohortsnoorder].commit    = ::Cohortsnoorder::commit_ro;
      stms[Cohortsnoorder].read      = ::Cohortsnoorder::read_ro;
      stms[Cohortsnoorder].write     = ::Cohortsnoorder::write_ro;
      stms[Cohortsnoorder].rollback  = ::Cohortsnoorder::rollback;
      stms[Cohortsnoorder].irrevoc   = ::Cohortsnoorder::irrevoc;
      stms[Cohortsnoorder].switcher  = ::Cohortsnoorder::onSwitchTo;
      stms[Cohortsnoorder].privatization_safe = false;
  }
}
