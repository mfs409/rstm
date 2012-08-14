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

#include "algs.hpp"
#include "../Diagnostics.hpp"

// ThreadID associated with activity_array
#define th_id tx->id - 1
#define MY activity_array[th_id]

// Maxmium threads supported
// [mfs] why do we only support 8 threads
#define MAXTHREADS 12

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace stm
{
  // ThreadID associated array to record each txn's activity
  //
  // [mfs] Why not embed this in the descriptor?  We aren't trying to save on
  // cache misses...
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

  TM_FASTCALL void* PessimisticTMReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* PessimisticTMReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void PessimisticTMWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void PessimisticTMWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void PessimisticTMWriteReadOnly(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void PessimisticTMCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void PessimisticTMCommitRW(TX_LONE_PARAMETER);
  TM_FASTCALL void PessimisticTMCommitReadOnly(TX_LONE_PARAMETER);

  /**
   *  PessimisticTM begin:
   *  Master thread set cntr from even to odd.
   */
  void PessimisticTMBegin(TX_LONE_PARAMETER)
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
          GoTurbo(tx, PessimisticTMReadRO, PessimisticTMWriteReadOnly,
                  PessimisticTMCommitReadOnly);
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
          GoTurbo(tx, PessimisticTMReadRW, PessimisticTMWriteRW, PessimisticTMCommitRW);
      }
  }

  /**
   *  PessimisticTM commit (read-only):
   *  Read-only transaction commit immediately
   */
  void
  PessimisticTMCommitReadOnly(TX_LONE_PARAMETER)
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
  PessimisticTMCommitRO(TX_LONE_PARAMETER)
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
  PessimisticTMCommitRW(TX_LONE_PARAMETER)
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
      ResetToRO(tx, PessimisticTMReadRO, PessimisticTMWriteRO, PessimisticTMCommitRO);
  }

  /**
   *  PessimisticTM read (read-only transaction)
   */
  void*
  PessimisticTMReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
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
  PessimisticTMReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse ReadRO barrier
      void* val = PessimisticTMReadRO(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  PessimisticTM write (for read-only transactions): Do nothing
   */
  void PessimisticTMWriteReadOnly(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(,,))
  {
      printf("Read-only tx called writes!\n");
      return;
  }

  /**
   *  PessimisticTM write (read-only context): for first write
   */
  void
  PessimisticTMWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // Add to write set
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, PessimisticTMReadRW, PessimisticTMWriteRW, PessimisticTMCommitRW);
  }

  /**
   *  PessimisticTM write (writing context)
   */
  void
  PessimisticTMWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  PessimisticTM unwinder:
   */
  void PessimisticTMRollback(STM_ROLLBACK_SIG(,,))
  {
      UNRECOVERABLE("PessimisticTM should never call rollback");
  }

  /**
   *  PessimisticTM in-flight irrevocability:
   */
  bool
  PessimisticTMIrrevoc(TxThread*)
  {
      UNRECOVERABLE("PessimisticTM Irrevocability not yet supported");
      return false;
  }

  /**
   *  Switch to PessimisticTM:
   *
   */
  void
  PessimisticTMOnSwitchTo()
  {
      writer_lock.val = 0;
      global_version.val = 1;
  }

  /**
   *  PessimisticTM initialization
   */
  template<>
  void initTM<PessimisticTM>()
  {
      // set the name
      stms[PessimisticTM].name      = "PessimisticTM";
      // set the pointers
      stms[PessimisticTM].begin     = PessimisticTMBegin;
      stms[PessimisticTM].commit    = PessimisticTMCommitRO;
      stms[PessimisticTM].read      = PessimisticTMReadRO;
      stms[PessimisticTM].write     = PessimisticTMWriteRO;
      stms[PessimisticTM].rollback  = PessimisticTMRollback;
      stms[PessimisticTM].irrevoc   = PessimisticTMIrrevoc;
      stms[PessimisticTM].switcher  = PessimisticTMOnSwitchTo;
      stms[PessimisticTM].privatization_safe = true;
  }
}


#ifdef STM_ONESHOT_ALG_PessimisticTM
DECLARE_AS_ONESHOT_NORMAL(PessimisticTM)
#endif
