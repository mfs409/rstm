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
using stm::WriteSet;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;

using stm::ValueList;
using stm::ValueListEntry;
using stm::gatekeeper;
using stm::last_order;
using stm::cohorts_node_t;

#define NOTDONE 3
#define DONE    4
/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace
{
  NOINLINE bool validate(TxThread* tx);
  struct pad_counter
  {
      volatile uint32_t val;
      char _padding_[128-sizeof(uint32_t)];
  };
  struct pad_counter counter = {0,{0}};
  struct cohorts_node_t* volatile q = NULL;

  struct CohortsLNI2Q
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
  };

  /**
   *  CohortsLNI2Q begin:
   *  CohortsLNI2Q has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishe their
   *  commits.
   */
  void CohortsLNI2Q::begin()
  {
      TxThread* tx = stm::Self;
      tx->allocator.onTxBegin();
    S1:
      // wait if I'm blocked
      while (q != NULL);

      // set started
      tx->status = COHORTS_STARTED;

      // double check no one is ready to commit
      if (q != NULL) {
          // [mfs] verify that no fences are needed here
          // [wer210] I don't think we need fence here...
          //          although verified by only testing benches
          tx->status = COHORTS_COMMITTED;
          goto S1;
      }
      tx->turn.val = NOTDONE;
  }

  /**
   *  CohortsLNI2Q commit (read-only):
   */
  void CohortsLNI2Q::commit_ro()
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
   *  CohortsLNI2Q commit_turbo (for write in place tx use):
   */
  void CohortsLNI2Q::commit_turbo()
  {
      TxThread* tx = stm::Self;
      // Mark self committed
      tx->status = COHORTS_COMMITTED;

      // clean up
      tx->vlist.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsLNI2Q commit (writing context):
   */
  void CohortsLNI2Q::commit_rw()
  {
      TxThread* tx = stm::Self;
      // add myself to the queue
      do {
          tx->turn.next = q;
      }while (!bcasptr(&q, tx->turn.next, &(tx->turn)));

      // Mark self pending to commit
      tx->status = COHORTS_CPENDING;
      WBR;

      uint32_t left = 0;
      for (uint32_t i = 0; i < threadcount.val; ++i)
          left += (threads[i]->status & 1);
      // if only one tx left, set global flag, inplace allowed
      //
      // [mfs] this is dangerous: it is possible for me to write 1, and
      //       then you to write 0 if you finish the loop first, but
      //       delay before reaching this line
      // [wer210] it doesn't matter cuz write 0 is not dangerous,
      //           just forbit one possible inplace write.
      counter.val = (left == 1);

      // Not first one? wait for your turn
      if (tx->turn.next != NULL)
          while (tx->turn.next->val != DONE);
      else
          // First one in a cohort waits until all tx are ready to commit
          for (uint32_t i = 0; i < threadcount.val; ++i)
              while (threads[i]->status == COHORTS_STARTED);

      // Everyone must validate read
      if (!validate(tx)) {
          tx->turn.val = DONE;
          tx->status = COHORTS_COMMITTED;
          if (q == &(tx->turn)) {
              q = NULL;
              counter.val = 0;
          }
          tx->tmabort();
      }

      // Do write back
      tx->writes.writeback();
      CFENCE;

      // Mark self status
      tx->turn.val = DONE;
      tx->status = COHORTS_COMMITTED;
      WBR; // this one cannot be omitted...

      // last one in a cohort reset q
      if (q == &(tx->turn)) {
          q = NULL;
          counter.val = 0;
      }

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsLNI2Q read (read-only transaction)
   */
  void* CohortsLNI2Q::read_ro(STM_READ_SIG(addr,))
  {
      TxThread* tx = stm::Self;
      void* tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsLNI2Q read_turbo (for write in place tx use)
   */
  void* CohortsLNI2Q::read_turbo(STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsLNI2Q read (writing transaction)
   */
  void* CohortsLNI2Q::read_rw(STM_READ_SIG(addr,mask))
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
   *  CohortsLNI2Q write (read-only context): for first write
   */
  void CohortsLNI2Q::write_ro(STM_WRITE_SIG(addr,val,mask))
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
      if (counter.val == 1) {
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
   *  CohortsLNI2Q write_turbo: for write in place tx
   */
  void CohortsLNI2Q::write_turbo(STM_WRITE_SIG(addr,val,mask))
  {
      // [mfs] ultimately this should use a macro that employs the mask
      *addr = val;
  }

  /**
   *  CohortsLNI2Q write (writing context)
   */
  void CohortsLNI2Q::write_rw(STM_WRITE_SIG(addr,val,mask))
  {
      TxThread* tx = stm::Self;
      // record the new value in a redo log
      //
      // [mfs] we might get better instruction scheduling if we put this code
      //       first, and then did the check.
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      // check if I can go turbo
      //
      // [mfs] this should be marked unlikely
      if (counter.val == 1) {
          // [mfs] I changed this to use the writeback() method, but it might
          //       have some overhead that we should avoid, depending on how
          //       it handles stack writes.
          tx->writes.writeback();
          // [mfs] I don't see why this fence is needed
          //CFENCE;
          *addr = val;
          // go turbo
          stm::GoTurbo(read_turbo, write_turbo, commit_turbo);
      }
  }

  /**
   *  CohortsLNI2Q unwinder:
   */
  void CohortsLNI2Q::rollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsLNI2Q in-flight irrevocability:
   */
  bool CohortsLNI2Q::irrevoc(TxThread*)
  {
      UNRECOVERABLE("CohortsLNI2Q Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsLNI2Q validation for commit: check that all reads are valid
   */
  bool
  validate(TxThread* tx)
  {
      foreach (ValueList, i, tx->vlist) {
          bool valid = STM_LOG_VALUE_IS_VALID(i, tx);
          if (!valid) return false;
      }
      return true;
  }

  /**
   *  Switch to CohortsLNI2Q:
   */
  void CohortsLNI2Q::onSwitchTo()
  {
      // when switching algs, mark all tx committed status
      for (uint32_t i = 0; i < threadcount.val; ++i) {
          threads[i]->status = COHORTS_COMMITTED;
      }
  }
}

namespace stm
{
  /**
   *  CohortsLNI2Q initialization
   */
  template<>
  void initTM<CohortsLNI2Q>()
  {
      // set the name
      stms[CohortsLNI2Q].name      = "CohortsLNI2Q";
      // set the pointers
      stms[CohortsLNI2Q].begin     = ::CohortsLNI2Q::begin;
      stms[CohortsLNI2Q].commit    = ::CohortsLNI2Q::commit_ro;
      stms[CohortsLNI2Q].read      = ::CohortsLNI2Q::read_ro;
      stms[CohortsLNI2Q].write     = ::CohortsLNI2Q::write_ro;
      stms[CohortsLNI2Q].rollback  = ::CohortsLNI2Q::rollback;
      stms[CohortsLNI2Q].irrevoc   = ::CohortsLNI2Q::irrevoc;
      stms[CohortsLNI2Q].switcher  = ::CohortsLNI2Q::onSwitchTo;
      stms[CohortsLNI2Q].privatization_safe = true;
  }
}

