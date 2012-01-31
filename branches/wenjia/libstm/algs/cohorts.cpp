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
 *  Cohorts Implementation
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

//#define DEBUG

using stm::TxThread;
using stm::threads;
using stm::threadcount;
using stm::last_complete;
using stm::timestamp;
using stm::timestamp_max;
using stm::WriteSet;
using stm::OrecList;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::orec_t;
using stm::get_orec;

using stm::tx_total;
using stm::tx_allowed;

#ifdef DEBUG
//debug output file
FILE *p;
#endif

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  struct Cohorts {
      static TM_FASTCALL bool begin(TxThread*);
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void commit_ro(TxThread* tx);
      static TM_FASTCALL void commit_rw(TxThread* tx);

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE void validate(TxThread* tx, uintptr_t finish_cache);
  };

  /**
   *  Cohorts begin:
   *  Cohorts has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finished their
   *  commitments.
   */
  bool
  Cohorts::begin(TxThread* tx)
  {
      // wait until we are allowed to start
      while (tx_allowed == 0){
          // in this wait loop, we need to check if an adaptivity action is
          // underway :(
          if (TxThread::tmbegin != begin)
              tx->tmabort(tx);
      }

#ifdef DEBUG
      p = fopen("/home/wer210/debug","a+"); fprintf(p,"Cohorts::begin.\n"); fclose(p);
#endif
      // get time of last finished txn, to know when to validate
      tx->ts_cache = last_complete.val;

      CFENCE;
      //add one to the total number of txs in a cohort.
      __sync_fetch_and_add ( &tx_total, 1 );

      tx->allocator.onTxBegin();

#ifdef DEBUG
      p = fopen("/home/wer210/debug","a+"); fprintf(p,"total tx = %d.\n",tx_total); fclose(p);
#endif

      return false;
  }

  /**
   *  Cohorts commit (read-only):
   *  RO commit is easy. But we need to check if tx is the last to commit in a
   *  cohorts, if so, we need to make tx_allowed 1 again.
   */
  void
  Cohorts::commit_ro(TxThread* tx)
  {
#ifdef DEBUG
      p = fopen("/home/wer210/debug","a+"); fprintf(p,"Cohorts::commit_ro.\n"); fclose(p);
#endif

      // before any commit, make sure tx_allowed is now 0, so that no tx is
      // allowed to start anymore
      if(tx_allowed == 1)
          tx_allowed = 0;
      /*
      // wait our turn, then validate
      while(last_complete.val !=((uintptr_t)tx->order -1)){
          // in this wait loop, we need to check if an adaptivity action is
          // underway :(
          if (TxThread::tmbegin != begin)
              tx->tmabort(tx);
              }

      foreach(OrecList, i, tx->r_orecs){
          //read this orec
          uintptr_t ivt = (*i)->v.all;
          //if it has a timestamp of ts_cache or greater, abort
          if(ivt > tx->ts_cache)
              tx->tmabort(tx);
      }

      // mark self as complete
      last_complete.val = tx->order;

      // set status to committed
      tx->order = -1;
      */

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      OnReadOnlyCommit(tx);

      //decrease one to the total number of txs waiting to commit in a cohort.
      __sync_fetch_and_add ( &tx_total, -1 );

      // if I'm the last one to commit, change tx_allowed back to 1
      if(tx_total == 0)
          __sync_val_compare_and_swap(&tx_allowed, 0, 1);

#ifdef DEBUG
      p = fopen("/home/wer210/debug","a+"); fprintf(p,"total tx = %d.\n",tx_total); fclose(p);
#endif
  }

  /**
   *  Cohorts commit (writing context):
   *
   *  RW commit is operated in turns. Transactions will be allowed to commit
   *  in an order which is given on the first write. Also, we need to convert
   *  tx_allowed into 1 if we are the last one to commit in a cohort.
   */
  void
  Cohorts::commit_rw(TxThread* tx)
  {
#ifdef DEBUG
      p = fopen("/home/wer210/debug","a+"); fprintf(p,"Cohorts::commit_rw.\n"); fclose(p);
#endif

      //before any commit, make sure tx_allowed is now 0
      if(tx_allowed == 1)
          tx_allowed = 0;

      // wait until it is our turn to commit, validate, and do writeback
      while (last_complete.val != (uintptr_t)(tx->order - 1)) {
          if (TxThread::tmbegin != begin)
              tx->tmabort(tx);
      }

      foreach(OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if it has a timestamp of ts_cache or greater, abort
          if ( ivt > tx->ts_cache)
              tx->tmabort(tx);
      }

      // mark every location in the write set, and perform write-back
      foreach (WriteSet, i, tx->writes) {
          // get orec
          orec_t* o = get_orec(i->addr);
          // mark orec
          o->v.all = tx->order;
          CFENCE; // WBW
          // write-back
          *i->addr = i->val;
      }
      last_complete.val = tx->order;

      // mark self as done
      last_complete.val = tx->order;

      // set status to committed...
      tx->order = -1;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);

      //decrease one to the total number of txs in a cohort.
      __sync_fetch_and_add ( &tx_total, -1 );

      // if I'm the last one to commit, change tx_allowed back to 1
      if(tx_total == 0)
          __sync_val_compare_and_swap(&tx_allowed, 0, 1);

#ifdef DEBUG
      p = fopen("/home/wer210/debug","a+"); fprintf(p,"total tx = %d.\n",tx_total); fclose(p);
#endif
  }

  /**
   *  Cohorts read (read-only transaction)
   *  Standard orec read function, but we don't need to validate
   */
  void*
  Cohorts::read_ro(STM_READ_SIG(tx,addr,))
  {
#ifdef DEBUG
      p = fopen("/home/wer210/debug","a+"); fprintf(p,"Cohorts::read_ro.\n"); fclose(p);
#endif

      void* tmp = *addr;

      /* CFENCE; // RBR between dereference and orec check

      // get the orec addr, read the orec's version#
      orec_t* o = get_orec(addr);
      uintptr_t ivt = o->v.all;
      // abort if this changed since the last time I saw someone finish
      //
      // NB: this is a pretty serious tradeoff... it admits false aborts for
      //     the sake of preventing a 'check if locked' test
      if (ivt > tx->ts_cache)
          tx->tmabort(tx);

      // log orec
      tx->r_orecs.insert(o);

      // validate
      if (last_complete.val > tx->ts_cache)
      validate(tx, last_complete.val);*/

      return tmp;
  }

  /**
   *  Cohorts read (writing transaction)
   *  For now, it is the same as pipeline read
   */
  void*
  Cohorts::read_rw(STM_READ_SIG(tx,addr,mask))
  {
#ifdef DEBUG
      p = fopen("/home/wer210/debug","a+"); fprintf(p,"Cohorts::read_rw.\n"); fclose(p);
#endif

      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      void* tmp = *addr;
      /*
      CFENCE;// RBR between dereference and orec check
      // get the orec addr, read the orec's version#
      orec_t* o = get_orec(addr);
      uintptr_t ivt = o->v.all;

      // abort if this changed since the last time I saw someone finish
      if (ivt > tx->ts_cache)
          tx->tmabort(tx);

      // log orec
      tx->r_orecs.insert(o);

      // validate if necessary
      if (last_complete.val > tx->ts_cache)
          validate(tx, last_complete.val);
      */

      REDO_RAW_CLEANUP(tmp, found, log, mask);
      return tmp;
  }

  /**
   *  Cohorts write (read-only context)
   *  NB: get order on first write
   */
  void
  Cohorts::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
#ifdef DEBUG
      p = fopen("/home/wer210/debug","a+"); fprintf(p,"Cohorts::write_rw.\n"); fclose(p);
#endif
      // we don't have any writes yet, so we need to get an order here
      tx->order = 1 + faiptr(&timestamp.val);

      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  Cohorts write (writing context)
   */
  void
  Cohorts::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
#ifdef DEBUG
      p = fopen("/home/wer210/debug","a+"); fprintf(p,"Cohorts::write_rw.\n"); fclose(p);
#endif
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  Cohorts unwinder:
   */
  stm::scope_t*
  Cohorts::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
#ifdef DEBUG
      p = fopen("/home/wer210/debug","a+"); fprintf(p,"Cohorts::rollback.\n"); fclose(p);
#endif

      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists, but keep any order we acquired
      tx->r_orecs.reset();
      tx->writes.reset();
      // NB: we can't reset pointers here, because if the transaction
      //     performed some writes, then it has an order.  If it has an
      //     order, but restarts and is read-only, then it still must call
      //     commit_rw to finish in-order
      return PostRollback(tx);
  }

  /**
   *  Cohorts in-flight irrevocability:
   */
  bool
  Cohorts::irrevoc(TxThread*)
  {
      UNRECOVERABLE("Cohorts Irrevocability not yet supported");
      return false;
  }

  /**
   *  Cohorts validation
   */
  void
  Cohorts::validate(TxThread* tx, uintptr_t finish_cache)
  {
#ifdef DEBUG
      p = fopen("/home/wer210/debug","a+"); fprintf(p,"Cohorts::validate.\n"); fclose(p);
#endif

      // check that all reads are valid
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if it has a timestamp of ts_cache or greater, abort
          if (ivt > tx->ts_cache)
              tx->tmabort(tx);
      }
      // now update the finish_cache to remember that at this time, we were
      // still valid
      tx->ts_cache = finish_cache;
  }

  /**
   *  Switch to Cohorts:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   *    Also, last_complete must equal timestamp
   *
   *    Also, all threads' order values must be -1
   */
  void
  Cohorts::onSwitchTo()
  {
#ifdef DEBUG
      p = fopen("/home/wer210/debug","a+"); fprintf(p,"Cohorts::onSwitchTo.\n"); fclose(p);
#endif

      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = timestamp.val;
      for (uint32_t i = 0; i < threadcount.val; ++i)
          threads[i]->order = -1;
  }
}

namespace stm {
  /**
   *  Cohorts initialization
   */
  template<>
  void initTM<Cohorts>()
  {
      // set the name
      stms[Cohorts].name      = "Cohorts";
      // set the pointers
      stms[Cohorts].begin     = ::Cohorts::begin;
      stms[Cohorts].commit    = ::Cohorts::commit_ro;
      stms[Cohorts].read      = ::Cohorts::read_ro;
      stms[Cohorts].write     = ::Cohorts::write_ro;
      stms[Cohorts].rollback  = ::Cohorts::rollback;
      stms[Cohorts].irrevoc   = ::Cohorts::irrevoc;
      stms[Cohorts].switcher  = ::Cohorts::onSwitchTo;
      stms[Cohorts].privatization_safe = true;
  }
}

