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
 *  CohortsLNI2 Implementation
 *
 *  CohortsLazy with inplace write when tx is the last one in a cohort.
 */
#include "../profiling.hpp"
#include "../algs.hpp"
#include "../RedoRAWUtils.hpp"

// define atomic operations
#define CAS __sync_val_compare_and_swap
#define ADD __sync_add_and_fetch
#define SUB __sync_sub_and_fetch

// define tx status
#define COHORTS_COMMITTED 0
#define COHORTS_STARTED   1
#define COHORTS_CPENDING  2

using stm::TxThread;
using stm::threads;
using stm::threadcount;
using stm::last_complete;
using stm::timestamp;
using stm::timestamp_max;
using stm::WriteSet;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;

using stm::ValueList;
using stm::ValueListEntry;
using stm::gatekeeper;
using stm::last_order;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace
{
  // [mfs] These should each probably be a pad_word_t, to keep them on
  //       separate cache lines
  volatile uintptr_t inplace = 0;
  volatile uintptr_t counter = 0;

  struct CohortsLNI2
  {
      static void begin();
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro();
      static TM_FASTCALL void commit_rw();

      static TM_FASTCALL void commit_turbo();
      static TM_FASTCALL void* read_turbo(STM_READ_SIG(,));
      static TM_FASTCALL void write_turbo(STM_WRITE_SIG(,,));

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static NOINLINE void validate(TxThread* tx);
  };

  /**
   *  CohortsLNI2 begin:
   *  CohortsLNI2 has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishe their
   *  commits.
   */
  void CohortsLNI2::begin()
  {
      TxThread* tx = stm::Self;
      tx->allocator.onTxBegin();

    S1:
      // wait if I'm blocked
      while (gatekeeper == 1);

      // set started
      //
      // [mfs] Using atomicswapptr is probably optimal on x86, but probably
      // not on SPARC or ARM.  For those architectures, we probably want
      // {tx->status = COHORTS_STARTED; WBR;}
      atomicswapptr(&tx->status, COHORTS_STARTED);

      // double check no one is ready to commit
      if (gatekeeper == 1 || inplace == 1) {
          // [mfs] verify that no fences are needed here
          tx->status = COHORTS_COMMITTED;
          goto S1;
      }

      // get time of last finished txn
      //
      // [mfs] this appears to be an unused variable
      tx->ts_cache = last_complete.val;
  }

  /**
   *  CohortsLNI2 commit (read-only):
   */
  void CohortsLNI2::commit_ro()
  {
      // [mfs] Do we need a read-write fence to ensure all reads are done
      //       before we write to tx->status?
      TxThread* tx = stm::Self;

      // mark self status
      tx->status = COHORTS_COMMITTED;

      // clean up
      tx->vlist.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  CohortsLNI2 commit_turbo (for write in place tx use):
   */
  void CohortsLNI2::commit_turbo()
  {
      TxThread* tx = stm::Self;

      // [mfs] it looks like we aren't using the queue technique... should
      //       we?  IIRC, the queue replaces the gatekeeper field.

      // Mark self pending to commit
      tx->status = COHORTS_CPENDING;

      // Get order
      //
      // [mfs] I don't understand why we need this...
      tx->order = 1 + faiptr(&timestamp.val);

      // Turbo tx can clean up first
      tx->vlist.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);

      // Wait for my turn
      //
      // [mfs] I do not understand why this waiting is required
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // Mark self as done
      last_complete.val = tx->order;

      // I must be the last one, so release gatekeeper lock
      last_order = tx->order + 1;
      gatekeeper = 0;
      counter = 0;

      // Reset inplace write flag
      inplace = 0;

      // Mark self status
      tx->status = COHORTS_COMMITTED;
  }

  /**
   *  CohortsLNI2 commit (writing context):
   */
  void CohortsLNI2::commit_rw()
  {
      TxThread* tx = stm::Self;

      // Mark a global flag, no one is allowed to begin now
      gatekeeper = 1;

      // Get an order
      tx->order = 1 + faiptr(&timestamp.val);

      // Mark self pending to commit
      tx->status = COHORTS_CPENDING;

      // For later use, indicates if I'm the last tx in this cohort
      bool lastone = true;

      uint32_t left = 1;
      while (left != 0) {
          left = 0;
          // [mfs] simplify with &1 instead of ==?
          for (uint32_t i = 0; i < threadcount.val; ++i)
              left += (threads[i]->status == COHORTS_STARTED);
          // if only one tx left, set global flag, inplace allowed
          //
          // [mfs] this is dangerous: it is possible for me to write 1, and
          //       then you to write 0 if you finish the loop first, but
          //       delay before reaching this line
          counter = (left == 1);
      }

      // wait for my turn to validate and do writeback
      //
      // [mfs] I think a queue would be faster here...
      while (last_complete.val != (uintptr_t)(tx->order - 1));

      // If I'm the first one in this cohort and no inplace write happened,
      // I will not do validation, else validate
      if (inplace == 1 || tx->order != last_order)
          validate(tx);

      // Do write back
      tx->writes.writeback();

      CFENCE;

      // Mark self as done... perhaps this and self status could be combined?
      last_complete.val = tx->order;

      // Mark self status
      tx->status = COHORTS_COMMITTED;
      WBR; // this one cannot be omitted...

      // Am I the last one?
      //
      // [mfs] Can this be done without iterating through all threads?  Can
      //       we use tx->order and timestamp.val?
      for (uint32_t i = 0;lastone != false && i < threadcount.val; ++i)
          lastone &= (threads[i]->status != COHORTS_CPENDING);

      // If I'm the last one, release gatekeeper lock
      if (lastone) {
          last_order = tx->order + 1;
          gatekeeper = 0;
          counter = 0;
      }

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsLNI2 read (read-only transaction)
   */
  void* CohortsLNI2::read_ro(STM_READ_SIG(addr,))
  {
      TxThread* tx = stm::Self;
      void* tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsLNI2 read_turbo (for write in place tx use)
   */
  void* CohortsLNI2::read_turbo(STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsLNI2 read (writing transaction)
   */
  void* CohortsLNI2::read_rw(STM_READ_SIG(addr,mask))
  {
      TxThread* tx = stm::Self;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      void* tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      REDO_RAW_CLEANUP(tmp, found, log, mask);
      return tmp;
  }

  /**
   *  CohortsLNI2 write (read-only context): for first write
   */
  void CohortsLNI2::write_ro(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // [mfs] this code is not in the best location.  Consider the following
      // alternative:
      //
      // - when a thread reaches the commit function, it seals the cohort
      // - then it counts the number of transactions in the cohort
      // - then it waits for all of them to finish
      // - while waiting, it eventually knows when there is exactly one left.
      // - at that point, it can set a flag to indicate that the last one is
      //   in-flight.
      // - all transactions can check that flag on every read/write
      //
      // There are a few challenges.  First, the current code waits on the
      // first thread, then the next, then the next...  Obviously that won't do
      // anymore.  Second, there can be a "flicker" when a thread sets a flag,
      // then reads the gatekeeper, then backs out.  Lastly, RO transactions
      // will require some sort of special attention.  But the tradeoff is more
      // potential to switch (not just first write), and without so much
      // redundant checking.
      if (counter == 1) {
          // set in place write flag
          //
          // [mfs] I don't see why this is atomic
          atomicswapptr(&inplace, 1);
          // write inplace
          //
          // [mfs] ultimately this should use a macro that employs the mask
          *addr = val;
          // switch to turbo mode
          stm::OnFirstWrite(read_turbo, write_turbo, commit_turbo);
          return;
      }

      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      stm::OnFirstWrite(read_rw, write_rw, commit_rw);
  }
  /**
   *  CohortsLNI2 write_turbo: for write in place tx
   */
  void CohortsLNI2::write_turbo(STM_WRITE_SIG(addr,val,mask))
  {
      // [mfs] ultimately this should use a macro that employs the mask
      *addr = val;
  }

  /**
   *  CohortsLNI2 write (writing context)
   */
  void CohortsLNI2::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;

      // check if I can go turbo
      //
      // [mfs] this should be marked unlikely
      if (counter == 1) {
          // setup inplace write flag
          //
          // [mfs] again, not sure why this is atomic
          atomicswapptr(&inplace, 1);
          // write previous write set back
          //
          // [mfs] I changed this to use the writeback() method, but it might
          //       have some overhead that we should avoid, depending on how
          //       it handles stack writes.
          tx->writes.writeback();

          // [mfs] I don't see why this fence is needed
          CFENCE;
          *addr = val;
          // go turbo
          stm::GoTurbo(read_turbo, write_turbo, commit_turbo);
          return;
      }

      // record the new value in a redo log
      //
      // [mfs] we might get better instruction scheduling if we put this code
      //       first, and then did the check.
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  CohortsLNI2 unwinder:
   */
  void CohortsLNI2::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists
      tx->vlist.reset();
      tx->writes.reset();

      PostRollback(tx);
  }

  /**
   *  CohortsLNI2 in-flight irrevocability:
   */
  bool CohortsLNI2::irrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsLNI2 Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsLNI2 validation for commit: check that all reads are valid
   */
  void CohortsLNI2::validate(TxThread* tx)
  {
      // [mfs] this is a pretty complex loop... since it is only called once,
      //       why not have validate return a boolean, and then drop out of
      //       the cohort from the commit code.
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid) {
              // Mark self status
              tx->status = COHORTS_COMMITTED;

              // Mark self as done
              last_complete.val = tx->order;
              // [mfs] The next WBR is commented out... why?  It seems
              //       important!
              //
              // WBR;

              // Am I the last one?
              bool l = true;
              for (uint32_t i = 0; l != false && i < threadcount.val; ++i)
                  l &= (threads[i]->status != COHORTS_CPENDING);

              // If I'm the last one, release gatekeeper lock
              if (l) {
                  last_order = tx->order + 1;
                  gatekeeper = 0;
                  counter = 0;
              }
              tx->tmabort();
          }
      }
  }

  /**
   *  Switch to CohortsLNI2:
   */
  void CohortsLNI2::onSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      last_complete.val = timestamp.val;
      // when switching algs, mark all tx committed status
      for (uint32_t i = 0; i < threadcount.val; ++i) {
          threads[i]->status = COHORTS_COMMITTED;
      }
  }
}

namespace stm
{
  /**
   *  CohortsLNI2 initialization
   */
  template<>
  void initTM<CohortsLNI2>()
  {
      // set the name
      stms[CohortsLNI2].name      = "CohortsLNI2";
      // set the pointers
      stms[CohortsLNI2].begin     = ::CohortsLNI2::begin;
      stms[CohortsLNI2].commit    = ::CohortsLNI2::commit_ro;
      stms[CohortsLNI2].read      = ::CohortsLNI2::read_ro;
      stms[CohortsLNI2].write     = ::CohortsLNI2::write_ro;
      stms[CohortsLNI2].rollback  = ::CohortsLNI2::rollback;
      stms[CohortsLNI2].irrevoc   = ::CohortsLNI2::irrevoc;
      stms[CohortsLNI2].switcher  = ::CohortsLNI2::onSwitchTo;
      stms[CohortsLNI2].privatization_safe = true;
  }
}

