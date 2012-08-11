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
 *  CohortsLNI2QX Implementation
 *
 *  CohortsLazy with inplace write when tx is the last one in a cohort.
 *  Early Seal CohortsLNI2Q
 */
#include "../profiling.hpp"
#include "../algs.hpp"
#include "../RedoRAWUtils.hpp"
#include "../Diagnostics.hpp"

using stm::TxThread;
using stm::threads;
using stm::threadcount;
using stm::WriteSet;
using stm::WriteSetEntry;

using stm::ValueList;
using stm::ValueListEntry;
using stm::gatekeeper;
using stm::last_order;
using stm::cohorts_node_t;

// # of reads/writes/aborts before seal a cohort
#define WRITE_EARLYSEAL  2
#define READ_EARLYSEAL  -1
#define ABORT_EARLYSEAL  -1

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace
{
  struct pad_word {
      volatile uint32_t val;
      char _padding_[128-sizeof(uint32_t)];
  };

  struct pad_word counter = {0,{0}};
  struct cohorts_node_t* volatile q = NULL;

  // gatekeeper for early seal
  struct pad_word sealed = {0,{0}};


  NOINLINE bool validate(TxThread* tx);
  struct CohortsLNI2QX
  {
      static void begin(TX_LONE_PARAMETER);
      static TM_FASTCALL void* read_ro(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void* read_rw(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void write_ro(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_rw(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit_ro(TX_LONE_PARAMETER);
      static TM_FASTCALL void commit_rw(TX_LONE_PARAMETER);

      static TM_FASTCALL void commit_turbo(TX_LONE_PARAMETER);
      static TM_FASTCALL void* read_turbo(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void write_turbo(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
  };

  /**
   *  CohortsLNI2QX begin:
   *  CohortsLNI2QX has a strict policy for transactions to begin. At first,
   *  every tx can start, until one of the tx is ready to commit. Then no
   *  tx is allowed to start until all the transactions finishe their
   *  commits.
   */
  void CohortsLNI2QX::begin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
    S1:
      // wait if I'm blocked
      while (q != NULL || sealed.val == 1);

      // set started
      tx->status = COHORTS_STARTED;
      WBR;

      // double check no one is ready to commit
      if (q != NULL || sealed.val == 1) {
          // [mfs] verify that no fences are needed here
          // [wer210] I don't think we need fence here...
          //          although verified by only testing benches
          tx->status = COHORTS_COMMITTED;
          goto S1;
      }
      // reset threadlocal variables
      tx->turn.val = COHORTS_NOTDONE;
      tx->cohort_writes = 0;
      // test if we need to do a early seal based on abort number
      if (tx->cohort_aborts == ABORT_EARLYSEAL) {
          atomicswap32(&sealed.val, 1);
          tx->cohort_aborts = 0;
      }
  }

  /**
   *  CohortsLNI2QX commit (read-only):
   */
  void CohortsLNI2QX::commit_ro(TX_LONE_PARAMETER)
  {
      // [mfs] Do we need a read-write fence to ensure all reads are done
      //       before we write to tx->status?
      TX_GET_TX_INTERNAL;
      // mark self status
      tx->status = COHORTS_COMMITTED;

      // clean up
      tx->vlist.reset();
      OnROCommit(tx);
  }

  /**
   *  CohortsLNI2QX commit_turbo (for write inplace tx use):
   */
  void CohortsLNI2QX::commit_turbo(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Mark self committed
      tx->status = COHORTS_COMMITTED;

      // clean up
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsLNI2QX commit (writing context):
   */
  void CohortsLNI2QX::commit_rw(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // pointer to the predecessor node in the queue
      struct cohorts_node_t* pred;

      // add myself to the queue
      //do {
      //    tx->turn.next = q;
      //} while (!bcasptr(&q, tx->turn.next, &(tx->turn)));
      pred = __sync_lock_test_and_set(&q, &(tx->turn));

      // Mark self pending to commit
      tx->status = COHORTS_CPENDING;
      WBR;

      // if only one tx left, set global flag, inplace allowed
      uint32_t left = 0;
      for (uint32_t i = 0; i < threadcount.val; ++i)
          left += (threads[i]->status & 1);
      //
      // [mfs] this is dangerous: it is possible for me to write 1, and
      //       then you to write 0 if you finish the loop first, but
      //       delay before reaching this line
      // [wer210] it doesn't matter cuz write 0 is not dangerous,
      //           just forbit one possible inplace write.
      counter.val = (left == 1);

      /*
      // Not first one? wait for your turn
      if (tx->turn.next != NULL)
          while (tx->turn.next->val != COHORTS_DONE);
      else {
          // First one in a cohort waits until all tx are ready to commit
          for (uint32_t i = 0; i < threadcount.val; ++i)
              while (threads[i]->status == COHORTS_STARTED);
      }
      */
      if (pred != NULL)
          while (pred->val != COHORTS_DONE);
      else {
          // First one in a cohort waits until all tx are ready to commit
          for (uint32_t i = 0; i < threadcount.val; ++i)
              while (threads[i]->status == COHORTS_STARTED);
      }

      // Everyone must validate read
      if (!validate(tx)) {
          // count the number of aborts
          tx->cohort_aborts ++;
          // mark self done
          tx->turn.val = COHORTS_DONE;
          if (q == &(tx->turn)) {
              counter.val = 0;
              CFENCE;
              sealed.val = 0;
              q = NULL;
          }
          stm::tmabort();
      }

      // Do write back
      tx->writes.writeback();
      CFENCE;

      // Mark self status
      tx->turn.val = COHORTS_DONE;

      // last one in a cohort reset q
      if (q == &(tx->turn)) {
          counter.val = 0;
          CFENCE;
          sealed.val = 0;
          q = NULL;
      }

      // commit all frees, reset all lists
      tx->vlist.reset();
      tx->writes.reset();
      OnRWCommit(tx);
      ResetToRO(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  CohortsLNI2QX read (read-only transaction)
   */
  void* CohortsLNI2QX::read_ro(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      void* tmp = *addr;
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  CohortsLNI2QX read_turbo (for write in place tx use)
   */
  void* CohortsLNI2QX::read_turbo(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CohortsLNI2QX read (writing transaction)
   */
  void* CohortsLNI2QX::read_rw(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
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
   *  CohortsLNI2QX write (read-only context): for first write
   */
  void CohortsLNI2QX::write_ro(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
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
          stm::OnFirstWrite(tx, read_turbo, write_turbo, commit_turbo);
          return;
      }

      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      stm::OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }
  /**
   *  CohortsLNI2QX write_turbo: for write in place tx
   */
  void CohortsLNI2QX::write_turbo(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      // [mfs] ultimately this should use a macro that employs the mask
      *addr = val;
  }

  /**
   *  CohortsLNI2QX write (writing context)
   */
  void CohortsLNI2QX::write_rw(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      //
      // [mfs] we might get better instruction scheduling if we put this code
      //       first, and then did the check.
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      tx->cohort_writes++;
      // test if we need to do a early seal based on write number
      if (tx->cohort_writes == WRITE_EARLYSEAL)
          atomicswap32(&sealed.val, 1);

      // check if I can go turbo
      //
      // [mfs] this should be marked unlikely
      if (counter.val == 1) {
          // [mfs] I changed this to use the writeback(TX_LONE_PARAMETER) method, but it might
          //       have some overhead that we should avoid, depending on how
          //       it handles stack writes.
          tx->writes.writeback();
          *addr = val;
          // go turbo
          stm::GoTurbo(tx, read_turbo, write_turbo, commit_turbo);
      }
  }

  /**
   *  CohortsLNI2QX unwinder:
   */
  void CohortsLNI2QX::rollback(STM_ROLLBACK_SIG(tx, except, len))
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
   *  CohortsLNI2QX in-flight irrevocability:
   */
  bool CohortsLNI2QX::irrevoc(TxThread*)
  {
      stm::UNRECOVERABLE("CohortsLNI2QX Irrevocability not yet supported");
      return false;
  }

  /**
   *  CohortsLNI2QX validation for commit: check that all reads are valid
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
   *  Switch to CohortsLNI2QX:
   */
  void CohortsLNI2QX::onSwitchTo()
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
   *  CohortsLNI2QX initialization
   */
  template<>
  void initTM<CohortsLNI2QX>()
  {
      // set the name
      stms[CohortsLNI2QX].name      = "CohortsLNI2QX";
      // set the pointers
      stms[CohortsLNI2QX].begin     = ::CohortsLNI2QX::begin;
      stms[CohortsLNI2QX].commit    = ::CohortsLNI2QX::commit_ro;
      stms[CohortsLNI2QX].read      = ::CohortsLNI2QX::read_ro;
      stms[CohortsLNI2QX].write     = ::CohortsLNI2QX::write_ro;
      stms[CohortsLNI2QX].rollback  = ::CohortsLNI2QX::rollback;
      stms[CohortsLNI2QX].irrevoc   = ::CohortsLNI2QX::irrevoc;
      stms[CohortsLNI2QX].switcher  = ::CohortsLNI2QX::onSwitchTo;
      stms[CohortsLNI2QX].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_CohortsLNI2QX
DECLARE_AS_ONESHOT_TURBO(CohortsLNI2QX)
#endif
