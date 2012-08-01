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
 *  Wealth Implementation
 *
 *    In this algorithm, all writer transactions are ordered by the time of
 *    their first write, and reader transactions are unordered.  By using
 *    ordering, in the form of a commit token, along with lazy acquire, we are
 *    able to provide strong progress guarantees and ELA semantics, while also
 *    avoiding atomic operations for acquiring orecs.
 */

#include "../profiling.hpp"
#include "../algs.hpp"
#include "../RedoRAWUtils.hpp"

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


/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  struct Wealth {
      static void begin();
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro();
      static TM_FASTCALL void commit_rw();

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE void validate(TxThread* tx, uintptr_t finish_cache);
  };

  /**
   *  Wealth begin:
   */
  void Wealth::begin()
  {
      TxThread* tx = stm::Self;
      tx->allocator.onTxBegin();
      // get time of last finished txn, to know when to validate
      tx->ts_cache = last_complete.val;
  }

  /**
   *  Wealth commit (read-only):
   */
  void
  Wealth::commit_ro()
  {
      TxThread* tx = stm::Self;
      // reset lists and we are done
      tx->r_orecs.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  Wealth commit (writing context):
   *
   *  NB:  Only valid if using pointer-based adaptivity
   */
  void
  Wealth::commit_rw()
  {
      TxThread* tx = stm::Self;
      // wait until it is our turn to commit, then validate, acquire, and do
      // writeback
      while (last_complete.val != (uintptr_t)(tx->order - 1)) {
          if (stm::tmbegin != begin)
              tx->tmabort();
      }

      // since we have the token, we can validate before getting locks
      validate(tx, last_complete.val);

      // if we had writes, then aborted, then restarted, and then didn't have
      // writes, we could end up trying to lock a nonexistant write set.  This
      // condition prevents that case.
      if (tx->writes.size() != 0) {
          // mark every location in the write set, and do write-back
          foreach (WriteSet, i, tx->writes) {
              // get orec
              orec_t* o = get_orec(i->addr);
              // mark orec
              o->v.all = tx->order;
              CFENCE; // WBW
              // write-back
              *i->addr = i->val;
          }
      }

      // mark self as done
      last_complete.val = tx->order;

      // set status to committed...
      tx->order = -1;

      // commit all frees, reset all lists
      tx->r_orecs.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  Wealth read (read-only transaction)
   */
  void*
  Wealth::read_ro(STM_READ_SIG(addr,))
  {
      TxThread* tx = stm::Self;
      // read the location... this is safe since timestamps behave as in Wang's
      // CGO07 paper
      void* tmp = *addr;
      CFENCE; // RBR between dereference and orec check

      // get the orec addr, read the orec's version#
      orec_t* o = get_orec(addr);
      uintptr_t ivt = o->v.all;
      // abort if this changed since the last time I saw someone finish
      //
      // NB: this is a pretty serious tradeoff... it admits false aborts for
      //     the sake of preventing a 'check if locked' test
      if (ivt > tx->ts_cache)
          tx->tmabort();

      // log orec
      tx->r_orecs.insert(o);

      // validate
      if (last_complete.val > tx->ts_cache)
          validate(tx, last_complete.val);
      return tmp;
  }

  /**
   *  Wealth read (writing transaction)
   */
  void*
  Wealth::read_rw(STM_READ_SIG(addr,mask))
  {
      TxThread* tx = stm::Self;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse the ReadRO barrier, which is adequate here---reduces LOC
      void* val = read_ro(addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  Wealth write (read-only context)
   */
  void
  Wealth::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // we don't have any writes yet, so we need to get an order here
      tx->order = 1 + faiptr(&timestamp.val);

      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      stm::OnFirstWrite(read_rw, write_rw, commit_rw);
  }

  /**
   *  Wealth write (writing context)
   */
  void
  Wealth::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  Wealth unwinder:
   */
  void
  Wealth::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
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
      PostRollback(tx);
  }

  /**
   *  Wealth in-flight irrevocability:
   */
  bool
  Wealth::irrevoc(TxThread*)
  {
      UNRECOVERABLE("Wealth Irrevocability not yet supported");
      return false;
  }

  /**
   *  Wealth validation
   */
  void
  Wealth::validate(TxThread* tx, uintptr_t finish_cache)
  {
      // check that all reads are valid
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if it has a timestamp of ts_cache or greater, abort
          if (ivt > tx->ts_cache)
              tx->tmabort();
      }
      // now update the finish_cache to remember that at this time, we were
      // still valid
      tx->ts_cache = finish_cache;
  }

  /**
   *  Switch to Wealth:
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
  Wealth::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = timestamp.val;
      for (uint32_t i = 0; i < threadcount.val; ++i)
          threads[i]->order = -1;
  }
}

namespace stm {
  /**
   *  Wealth initialization
   */
  template<>
  void initTM<Wealth>()
  {
      // set the name
      stms[Wealth].name      = "Wealth";
      // set the pointers
      stms[Wealth].begin     = ::Wealth::begin;
      stms[Wealth].commit    = ::Wealth::commit_ro;
      stms[Wealth].read      = ::Wealth::read_ro;
      stms[Wealth].write     = ::Wealth::write_ro;
      stms[Wealth].rollback  = ::Wealth::rollback;
      stms[Wealth].irrevoc   = ::Wealth::irrevoc;
      stms[Wealth].switcher  = ::Wealth::onSwitchTo;
      stms[Wealth].privatization_safe = true;
  }
}

