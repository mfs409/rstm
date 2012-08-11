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
 *  CTokenNOrec Implementation
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
#include "../Diagnostics.hpp"

using stm::TxThread;
using stm::threads;
using stm::threadcount;
using stm::last_complete;
using stm::timestamp;
using stm::timestamp_max;
using stm::WriteSet;
using stm::WriteSetEntry;

using stm::ValueList;
using stm::ValueListEntry;


/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  struct CTokenNOrec {
      static void begin(TX_LONE_PARAMETER);
      static TM_FASTCALL void* read_ro(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro(TX_LONE_PARAMETER);
      static TM_FASTCALL void commit_rw(TX_LONE_PARAMETER);

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE void validate(TxThread* tx, uintptr_t finish_cache);
  };

  /**
   *  CTokenNOrec begin:
   */
  void CTokenNOrec::begin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
      // get time of last finished txn, to know when to validate
      tx->ts_cache = last_complete.val;
  }

  /**
   *  CTokenNOrec commit (read-only):
   */
  void
  CTokenNOrec::commit_ro(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // reset lists and we are done
      tx->vlist.reset();
      OnROCommit(tx);
  }

  /**
   *  CTokenNOrec commit (writing context):
   *
   *  NB:  Only valid if using pointer-based adaptivity
   */
  void
  CTokenNOrec::commit_rw(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // wait until it is our turn to commit, then validate, acquire, and do
      // writeback
      while (last_complete.val != (uintptr_t)(tx->order - 1)) {
          // Check if we need to abort due to an adaptivity event
          if (stm::tmbegin != begin)
              stm::tmabort();
      }

      // since we have the token, we can validate before getting locks
      //
      // [mfs] should this be guarded with code like "if (last_complete.val >
      //       tx->ts_cache)" to prevent unnecessary validations by
      //       single-threaded code?
      if (last_complete.val > tx->ts_cache)
          validate(tx, last_complete.val);

      // if we had writes, then aborted, then restarted, and then didn't have
      // writes, we could end up trying to lock a nonexistant write set.  This
      // condition prevents that case.
      if (tx->writes.size() != 0) {
          // mark every location in the write set, and do write-back
          foreach (WriteSet, i, tx->writes) {
              // write-back
              *i->addr = i->val;
          }
      }

      // mark self as done
      last_complete.val = tx->order;

      // set status to committed...
      tx->order = -1;

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CTokenNOrec read (read-only transaction)
   */
  void*
  CTokenNOrec::read_ro(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // read the location
      void* tmp = *addr;
      // log
      STM_LOG_VALUE(tx, addr, tmp, mask);

      // validate
      //if (last_complete.val > tx->ts_cache)
      validate(tx, last_complete.val);

      return tmp;
  }

  /**
   *  CTokenNOrec read (writing transaction)
   */
  void*
  CTokenNOrec::read_rw(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse the ReadRO barrier, which is adequate here---reduces LOC
      void* val = read_ro(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  CTokenNOrec write (read-only context)
   */
  void
  CTokenNOrec::write_ro(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // we don't have any writes yet, so we need to get an order here
      tx->order = 1 + faiptr(&timestamp.val);

      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      stm::OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  CTokenNOrec write (writing context)
   */
  void
  CTokenNOrec::write_rw(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CTokenNOrec unwinder:
   */
  void
  CTokenNOrec::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists, but keep any order we acquired
      tx->vlist.reset();
      tx->writes.reset();
      // NB: we can't reset pointers here, because if the transaction
      //     performed some writes, then it has an order.  If it has an
      //     order, but restarts and is read-only, then it still must call
      //     commit_rw to finish in-order
      PostRollback(tx);
  }

  /**
   *  CTokenNOrec in-flight irrevocability:
   */
  bool
  CTokenNOrec::irrevoc(TxThread*)
  {
      stm::UNRECOVERABLE("CTokenNOrec Irrevocability not yet supported");
      return false;
  }

  /**
   *  CTokenNOrec validation
   */
  void
  CTokenNOrec::validate(TxThread* tx, uintptr_t finish_cache)
  {
      // check that all reads are valid
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid)
              stm::tmabort();
      }

      // now update the finish_cache to remember that at this time, we were
      // still valid
      tx->ts_cache = finish_cache;
  }

  /**
   *  Switch to CTokenNOrec:
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
  CTokenNOrec::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = timestamp.val;
      for (uint32_t i = 0; i < threadcount.val; ++i)
          threads[i]->order = -1;
  }
}

namespace stm {
  /**
   *  CTokenNOrec initialization
   */
  template<>
  void initTM<CTokenNOrec>()
  {
      // set the name
      stms[CTokenNOrec].name      = "CTokenNOrec";
      // set the pointers
      stms[CTokenNOrec].begin     = ::CTokenNOrec::begin;
      stms[CTokenNOrec].commit    = ::CTokenNOrec::commit_ro;
      stms[CTokenNOrec].read      = ::CTokenNOrec::read_ro;
      stms[CTokenNOrec].write     = ::CTokenNOrec::write_ro;
      stms[CTokenNOrec].rollback  = ::CTokenNOrec::rollback;
      stms[CTokenNOrec].irrevoc   = ::CTokenNOrec::irrevoc;
      stms[CTokenNOrec].switcher  = ::CTokenNOrec::onSwitchTo;
      stms[CTokenNOrec].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_CTokenNOrec
DECLARE_AS_ONESHOT_NORMAL(CTokenNOrec)
#endif
