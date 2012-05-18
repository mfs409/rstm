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
 *  PTM Implementation
 *
 *  Based on A.Matveev et.al's paper "Towards a Fully Pessimistic STM
 *  Model", TRANSACT'12, FEB.2012
 */

#include "profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

// ThreadID associated with activity_array
#define th_id tx->id - 1
#define MY activity_array[th_id]

// Maxmium threads supported
// [mfs] why do we only support 8 threads
#define MAXTHREADS 12

using stm::TxThread;
using stm::WriteSet;
using stm::UNRECOVERABLE;
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
      {{0xFFFFFFFF, false},{0xFFFFFFFF, false},{0xFFFFFFFF, false},
       {0xFFFFFFFF, false},{0xFFFFFFFF, false},{0xFFFFFFFF, false},
       {0xFFFFFFFF, false},{0xFFFFFFFF, false},{0xFFFFFFFF, false},
       {0xFFFFFFFF, false},{0xFFFFFFFF, false},{0xFFFFFFFF, false}};

  struct PTM {
      static TM_FASTCALL bool begin(TxThread*);
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_read_only(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void commit_ro(TxThread* tx);
      static TM_FASTCALL void commit_rw(TxThread* tx);
      static TM_FASTCALL void commit_read_only(TxThread* tx);

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
  };

  /**
   *  PTM begin:
   *  Master thread set cntr from even to odd.
   */
  bool
  PTM::begin(TxThread* tx)
  {
      // starts
      tx->allocator.onTxBegin();

      // For Read-Only transactions
      if (tx->read_only) {
          // Read the global version to tx_version
          MY.tx_version = global_version.val;


          // [mfs] This should not be necessary, since the cleanup from the
          //       last transaction should have reset these pointer.
          //
          // [wer210] read-onlyness may change
          // go read-only mode
          GoTurbo(tx, read_ro, write_read_only, commit_read_only);
      }
      // For Read-Write transactions
      else
      {
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
          GoTurbo(tx, read_rw, write_rw, commit_rw);
      }
      return true;
  }

  /**
   *  PTM commit (read-only):
   *  Read-only transaction commit immediately
   */
  void
  PTM::commit_read_only(TxThread* tx)
  {
      // Set the tx_version to the maximum value
      MY.tx_version = 0xFFFFFFFF;

      // clean up
      tx->progress_is_seen = false;
      tx->read_only = false;
      OnReadOnlyCommit(tx);
  }

  /**
   *  PTM commit (read-only):
   *  For those who did not mark themselves read_only
   *  at the begining of each transactions, but who do not have any writes
   *
   *  [mfs] Is this optimal?  There might be a fast path we can employ here.
   */
  void
  PTM::commit_ro(TxThread* tx)
  {
      // Set the tx_version to the maximum value
      MY.tx_version = 0xFFFFFFFF;

      // clean up
      tx->progress_is_seen = false;
      tx->read_only = false;
      OnReadOnlyCommit(tx);

  }

  /**
   *  PTM commit (writing context):
   *
   *  [mfs] This function needs more documentation.  The algorithm is not
   *        particularly clear from the code.
   */
  void
  PTM::commit_rw(TxThread* tx)
  {
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
      atomicswap32(&global_version.val, global_version.val + 1);

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
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  PTM read (read-only transaction)
   */
  void*
  PTM::read_ro(STM_READ_SIG(tx,addr,))
  {
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
   *  PTM read (writing transaction)
   */
  void*
  PTM::read_rw(STM_READ_SIG(tx,addr,mask))
  {
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse read_ro barrier
      void* val = read_ro(tx, addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  PTM write (for read-only transactions): Do nothing
   */
  void
  PTM::write_read_only(STM_WRITE_SIG(tx,addr,val,mask))
  {
      printf("Read-only tx called writes!\n");
      return;
  }

  /**
   *  PTM write (read-only context): for first write
   */
  void
  PTM::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // Add to write set
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  PTM write (writing context)
   */
  void
  PTM::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // record the new value in a redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  PTM unwinder:
   */
  stm::scope_t*
  PTM::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      return NULL;
  }

  /**
   *  PTM in-flight irrevocability:
   */
  bool
  PTM::irrevoc(TxThread*)
  {
      UNRECOVERABLE("PTM Irrevocability not yet supported");
      return false;
  }

  /**
   *  Switch to PTM:
   *
   */
  void
  PTM::onSwitchTo()
  {
      writer_lock.val = 0;
      global_version.val = 1;
  }
}

namespace stm {
  /**
   *  PTM initialization
   */
  template<>
  void initTM<PTM>()
  {
      // set the name
      stms[PTM].name      = "PTM";
      // set the pointers
      stms[PTM].begin     = ::PTM::begin;
      stms[PTM].commit    = ::PTM::commit_ro;
      stms[PTM].read      = ::PTM::read_ro;
      stms[PTM].write     = ::PTM::write_ro;
      stms[PTM].rollback  = ::PTM::rollback;
      stms[PTM].irrevoc   = ::PTM::irrevoc;
      stms[PTM].switcher  = ::PTM::onSwitchTo;
      stms[PTM].privatization_safe = true;
  }
}

