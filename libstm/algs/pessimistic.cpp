B1;2c/**
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

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

// ThreadID associated with activity_array
#define th_id tx->id - 1
#define MY activity_array[th_id]

// Maxmium threads supported
#define MAXTHREADS 8

using stm::TxThread;
using stm::WriteSet;
using stm::UNRECOVERABLE;
using stm::WriteSetEntry;
using stm::orec_t;
using stm::get_orec;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  // ThreadID associated array to record each txn's activity
  struct Activity {
     volatile uint32_t tx_version;
     volatile bool writer_waiting;
  };
  static struct Activity activity_array[MAXTHREADS] =
      {{0xFFFFFFFF, false},{0xFFFFFFFF, false},{0xFFFFFFFF, false},
       {0xFFFFFFFF, false},{0xFFFFFFFF, false},{0xFFFFFFFF, false},
       {0xFFFFFFFF, false},{0xFFFFFFFF, false}};

  volatile uint32_t global_version = 1;
  volatile uint32_t writer_lock = 0;

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
      static NOINLINE void UpdateWriteSetVersions(TxThread* tx, uint32_t version);
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
          MY.tx_version = global_version;

          // go read-only mode
          GoTurbo(tx, read_ro, write_read_only, commit_read_only);
      }
      // For Read-Write transactions
      else
      {
          // Set the thread's entry writer_waiting to TRUE
          MY.writer_waiting = true;

          // Try to aquire the gloabl lock, and set myself wait-free
          while (MY.writer_waiting == true) {
              if (bcas32(&writer_lock, 0, 1))
                  MY.writer_waiting = false;
          }

          // Read the global version to tx_version
          MY.tx_version = global_version;

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
      // Set the tx_version maximum value
      MY.tx_version = 0xFFFFFFFF;

      // clean up
      tx->progress_is_seen = false;
      tx->read_only = false;
      OnReadOnlyCommit(tx);
  }

  /**
   *  PTM commit (read-only):
   *  For those who did not mark themselves read_only
   *  at the begining of each transactions
   */
  void
  PTM::commit_ro(TxThread* tx)
  {
      commit_rw(tx);
  }

  /**
   *  PTM commit (writing context):
   *
   */
  void
  PTM::commit_rw(TxThread* tx)
  {
      // Wait if tx_version is even
      if ((MY.tx_version & 0x01) == 0) {
          // Wait for version progress
          while (global_version == MY.tx_version)
              spin64();
          MY.tx_version = global_version;
      }

      // Mark orecs of locations in Writeset, version is (tx_version + 1)
      UpdateWriteSetVersions(tx, MY.tx_version + 1);

      // First global version increment, global_version will be even
      global_version ++;
      WBR;
      // update my local version
      MY.tx_version = global_version;

      // Signal the next writer
      // Scan from (th_id + 1) to the end of the array
      // and start over from 0 to the (th_id)
      for (uint32_t i = 1; i <= MAXTHREADS; i++) {
          if (activity_array[(i+th_id) % MAXTHREADS].writer_waiting == true) {
              CFENCE;
              activity_array[(i+th_id) % MAXTHREADS].writer_waiting = false;
              CFENCE;
              goto NEXT;
          }
      }

      // Otherwise, release the global writer_lock
      writer_lock = 0;

    NEXT:
      // Quiescence, wait for all read-only tx started before
      // first global version increment to finish their commits
      for (uint32_t k = 0; k < MAXTHREADS; ++k)
          while (activity_array[k].tx_version < MY.tx_version)
              spin64();

      // Now do write back
      foreach (WriteSet, i, tx->writes)
          *i->addr = i->val;
      WBR; //WBW

      //Second global version increment, now global_version becomes odd
      global_version ++;

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
      if (tx->progress_is_seen != true) {
          orec_t *o = get_orec(addr);
          if (o->v.all != MY.tx_version)
              return *addr;
          // A writer has not yet finished writeback, Wait for version progress
          while (global_version == MY.tx_version)
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
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // reset all lists
      tx->writes.reset();

      return PostRollback(tx);
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
   *  PTM helper function: Write (tx_version + 1) as Orec version
   */
  void
  PTM::UpdateWriteSetVersions(TxThread* tx, uint32_t version)
  {
      foreach (WriteSet, i, tx->writes) {
          // get orec
          orec_t* o = get_orec(i->addr);
          // mark orec
          o->v.all = version;
      }
  }

  /**
   *  Switch to PTM:
   *
   */
  void
  PTM::onSwitchTo()
  {
      writer_lock = 0;
      global_version = 1;
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

