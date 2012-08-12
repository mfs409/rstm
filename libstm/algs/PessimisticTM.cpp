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
 *  PessimisticTM Implementation
 *
 *  Based on A.Matveev et.al's paper "Towards a Fully Pessimistic STM
 *  Model", TRANSACT'12, FEB.2012
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "../RedoRAWUtils.hpp"
#include "../Diagnostics.hpp"

// ThreadID associated with activity_array
#define th_id tx->id - 1
#define MY activity_array[th_id]

// Maxmium threads supported
// [mfs] why do we only support 8 threads
#define MAXTHREADS 12

using stm::TxThread;
using stm::WriteSet;
using stm::WriteSetEntry;
using stm::orec_t;
using stm::get_orec;
using stm::global_version;
using stm::writer_lock;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  // ThreadID associated array to record each txn's activity
  struct Activity {
      volatile uint32_t tx_version;
      volatile bool writer_waiting;
      char _padding_[128-sizeof(uint32_t)-sizeof(bool)];
  };
  static struct Activity activity_array[MAXTHREADS] =
      {{0xFFFFFFFF, false, {0}},{0xFFFFFFFF, false, {0}},{0xFFFFFFFF, false, {0}},
       {0xFFFFFFFF, false, {0}},{0xFFFFFFFF, false, {0}},{0xFFFFFFFF, false, {0}},
       {0xFFFFFFFF, false, {0}},{0xFFFFFFFF, false, {0}},{0xFFFFFFFF, false, {0}},
       {0xFFFFFFFF, false, {0}},{0xFFFFFFFF, false, {0}},{0xFFFFFFFF, false, {0}}};

  struct PessimisticTM {
      static void begin(TX_LONE_PARAMETER);
      static TM_FASTCALL void* ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void* ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void write_read_only(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void CommitRO(TX_LONE_PARAMETER);
      static TM_FASTCALL void CommitRW(TX_LONE_PARAMETER);
      static TM_FASTCALL void commit_read_only(TX_LONE_PARAMETER);

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
  };

  /**
   *  PessimisticTM begin:
   *  Master thread set cntr from even to odd.
   */
  void PessimisticTM::begin(TX_LONE_PARAMETER)
  {
#ifdef STM_ONESHOT_ALG_PessimisticTM
      assert(0 && "PessimisticTM not yet ported to oneshot build");
#endif
      TX_GET_TX_INTERNAL;
      // starts
      tx->allocator.onTxBegin();

      // For Read-Only transactions
      if (tx->read_only) {
          // Read the global version to tx_version
          MY.tx_version = global_version.val;
          // go read-only mode
          stm::GoTurbo(tx, ReadRO, write_read_only, commit_read_only);
      }
      // For Read-Write transactions
      else {
          // Set the thread's entry writer_waiting to TRUE
          MY.writer_waiting = true;

          // Try to aquire the global lock, and set myself wait-free
          //
          // NB: since we've got the baton mechanism for passing the writer
          // token, we may not actually need to do the CAS to get the lock.
          //
          // [mfs] Should we use TAS instead of CAS?  It's probably cheaper.
          //       Also, we probably want some sort of backoff or at least a
          //       test before the CAS to prevent bus traffic.
          while (MY.writer_waiting == true) {
              if (writer_lock.val == 0 && bcas32(&writer_lock.val, 0, 1))
                  MY.writer_waiting = false;
              else
                  spin64();
          }

          // Read the global version to tx_version
          MY.tx_version = global_version.val;

          // Go read-write mode
          stm::GoTurbo(tx, ReadRW, WriteRW, CommitRW);
      }
  }

  /**
   *  PessimisticTM commit (read-only):
   *  Read-only transaction commit immediately
   */
  void
  PessimisticTM::commit_read_only(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Set the tx_version to the maximum value
      MY.tx_version = 0xFFFFFFFF;

      // clean up
      tx->progress_is_seen = false;
      tx->read_only = false;
      OnROCommit(tx);
  }

  /**
   *  PessimisticTM commit (read-only):
   *  For those who did not mark themselves read_only
   *  at the begining of each transactions, but who do not have any writes
   *
   *  [mfs] Is this optimal?  There might be a fast path we can employ here.
   */
  void
  PessimisticTM::CommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Set the tx_version to the maximum value
      MY.tx_version = 0xFFFFFFFF;

      // clean up
      tx->progress_is_seen = false;
      tx->read_only = false;
      OnROCommit(tx);

  }

  /**
   *  PessimisticTM commit (writing context):
   *
   *  [mfs] This function needs more documentation.  The algorithm is not
   *        particularly clear from the code.
   */
  void
  PessimisticTM::CommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Wait if tx_version is even
      if ((MY.tx_version & 0x01) == 0) {
          // Wait for version progress
          while (global_version.val == MY.tx_version)
              spin64();
          MY.tx_version = global_version.val;
      }

      // Mark orecs of locations in Writeset, version is (tx_version + 1)
      uint32_t version = MY.tx_version + 1;
      foreach (WriteSet, i, tx->writes) {
          // get orec
          orec_t* o = get_orec(i->addr);
          // mark orec
          o->v.all = version;
      }

      // First global version increment, global_version.val will be even
      //
      // [mfs] I'm guessing that we need WBR ordering here?  In any case, to
      //       port to SPARC I'm using a WBR instead of a swap, since it
      //       should be faster.
#ifdef STM_CPU_X86
      atomicswap32(&global_version.val, global_version.val + 1);
#else
      CFENCE;
      global_version.val++;
      WBR;
#endif

      // update my local version
      MY.tx_version = global_version.val;

      // Signal the next writer
      // Scan from (th_id + 1) to the end of the array
      // and start over from 0 to the (th_id)
      bool found = false;
      for (uint32_t i = 1; i <= MAXTHREADS; i++) {
          if (activity_array[(i+th_id) % MAXTHREADS].writer_waiting == true) {
              activity_array[(i+th_id) % MAXTHREADS].writer_waiting = false;
              found = true;
              break;
          }
      }

      if (!found)
          // Otherwise, release the global writer_lock.val
          writer_lock.val = 0;

      // Quiescence, wait for all read-only tx started before
      // first global version increment to finish their commits
      for (uint32_t k = 0; k < MAXTHREADS; ++k)
          while (activity_array[k].tx_version < MY.tx_version)
              spin64();

      // Now do write back
      foreach (WriteSet, i, tx->writes)
          *i->addr = i->val;

      CFENCE; //WBW

      // Second global version increment, now global_version.val becomes odd
      global_version.val = MY.tx_version + 1;

      // Set the tx_version maximum value
      MY.tx_version = 0xFFFFFFFF;

      // commit all frees, reset all lists
      tx->writes.reset();
      tx->progress_is_seen = false;
      OnRWCommit(tx);
      ResetToRO(tx, ReadRO, WriteRO, CommitRO);
  }

  /**
   *  PessimisticTM read (read-only transaction)
   */
  void*
  PessimisticTM::ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // read_only tx only wait for one round at most
      //
      // [mfs] We could use multiple versions of the read instrumentation to
      //       work around this without any branches.  We could also use some
      //       sort of notification so that a completed writeback would allow
      //       this reader to never need to check again.
      if (tx->progress_is_seen != true) {
          orec_t *o = get_orec(addr);
          if (o->v.all != MY.tx_version)
              return *addr;
          // A writer has not yet finished writeback, Wait for version progress
          while (global_version.val == MY.tx_version)
              spin64();
          tx->progress_is_seen = true;
      }
      return *addr;
  }

  /**
   *  PessimisticTM read (writing transaction)
   */
  void*
  PessimisticTM::ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse ReadRO barrier
      void* val = ReadRO(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  PessimisticTM write (for read-only transactions): Do nothing
   */
  void PessimisticTM::write_read_only(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(,,))
  {
      printf("Read-only tx called writes!\n");
      return;
  }

  /**
   *  PessimisticTM write (read-only context): for first write
   */
  void
  PessimisticTM::WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // Add to write set
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      stm::OnFirstWrite(tx, ReadRW, WriteRW, CommitRW);
  }

  /**
   *  PessimisticTM write (writing context)
   */
  void
  PessimisticTM::WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  PessimisticTM unwinder:
   */
  void PessimisticTM::rollback(STM_ROLLBACK_SIG(,,))
  {
      stm::UNRECOVERABLE("PessimisticTM should never call rollback");
  }

  /**
   *  PessimisticTM in-flight irrevocability:
   */
  bool
  PessimisticTM::irrevoc(TxThread*)
  {
      stm::UNRECOVERABLE("PessimisticTM Irrevocability not yet supported");
      return false;
  }

  /**
   *  Switch to PessimisticTM:
   *
   */
  void
  PessimisticTM::onSwitchTo()
  {
      writer_lock.val = 0;
      global_version.val = 1;
  }
}

namespace stm {
  /**
   *  PessimisticTM initialization
   */
  template<>
  void initTM<PessimisticTM>()
  {
      // set the name
      stms[PessimisticTM].name      = "PessimisticTM";
      // set the pointers
      stms[PessimisticTM].begin     = ::PessimisticTM::begin;
      stms[PessimisticTM].commit    = ::PessimisticTM::CommitRO;
      stms[PessimisticTM].read      = ::PessimisticTM::ReadRO;
      stms[PessimisticTM].write     = ::PessimisticTM::WriteRO;
      stms[PessimisticTM].rollback  = ::PessimisticTM::rollback;
      stms[PessimisticTM].irrevoc   = ::PessimisticTM::irrevoc;
      stms[PessimisticTM].switcher  = ::PessimisticTM::onSwitchTo;
      stms[PessimisticTM].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_PessimisticTM
DECLARE_AS_ONESHOT_NORMAL(PessimisticTM)
#endif
