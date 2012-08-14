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
 *  OrecEagerRedo Implementation
 *
 *    This code is very similar to the TinySTM-writeback algorithm.  It can
 *    also be thought of as OrecEager with redo logs instead of undo logs.
 *    Note, though, that it uses timestamps as in Wang's CGO 2007 paper, so
 *    we always validate at commit time but we don't have to check orecs
 *    twice during each read.
 */

#include "algs.hpp"

namespace stm
{
  TM_FASTCALL void* OrecEagerRedoReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* OrecEagerRedoReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void OrecEagerRedoWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void OrecEagerRedoWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void OrecEagerRedoCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void OrecEagerRedoCommitRW(TX_LONE_PARAMETER);
  NOINLINE void OrecEagerRedoValidate(TxThread*);

  /**
   *  OrecEagerRedo begin:
   *
   *    Standard begin: just get a start time
   */
  void OrecEagerRedoBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
      tx->start_time = timestamp.val;
  }

  /**
   *  OrecEagerRedo commit (read-only):
   *
   *    Standard commit: we hold no locks, and we're valid, so just clean up
   */
  void
  OrecEagerRedoCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->r_orecs.reset();
      OnROCommit(tx);
  }

  /**
   *  OrecEagerRedo commit (writing context):
   *
   *    Since we hold all locks, and since we use Wang-style timestamps, we
   *    need to validate, run the redo log, and then get a timestamp and
   *    release locks.
   */
  void
  OrecEagerRedoCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // note: we're using timestamps in the same manner as
      // OrecLazy... without the single-thread optimization

      // we have all locks, so validate
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
              tmabort();
      }

      // run the redo log
      tx->writes.writeback();

      // we're a writer, so increment the global timestamp
      tx->end_time = 1 + faiptr(&timestamp.val);

      // release locks
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = tx->end_time;

      // clean up
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      OnRWCommit(tx);
      ResetToRO(tx, OrecEagerRedoReadRO, OrecEagerRedoWriteRO, OrecEagerRedoCommitRO);
  }

  /**
   *  OrecEagerRedo read (read-only transaction)
   *
   *    Since we don't hold locks in an RO transaction, this code is very
   *    simple: read the location, check the orec, and scale the timestamp if
   *    necessary.
   */
  void*
  OrecEagerRedoReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the location
          void* tmp = *addr;
          CFENCE;
          // read orec
          id_version_t ivt; ivt.all = o->v.all;

          // common case: new read to uncontended location
          if (ivt.all <= tx->start_time) {
              tx->r_orecs.insert(o);
              return tmp;
          }

          // abort if locked by other
          if (ivt.fields.lock)
              tmabort();

          // scale timestamp if ivt is too new
          uintptr_t newts = timestamp.val;
          OrecEagerRedoValidate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrecEagerRedo read (writing transaction)
   *
   *    The RW read code is slightly more complicated.  We only check the read
   *    log if we hold the lock, but we must be prepared for that possibility.
   */
  void*
  OrecEagerRedoReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the location
          void* tmp = *addr;
          CFENCE;
          // read orec
          id_version_t ivt; ivt.all = o->v.all;

          // common case: new read to uncontended location
          if (ivt.all <= tx->start_time) {
              tx->r_orecs.insert(o);
              return tmp;
          }

          // next best: locked by me
          if (ivt.all == tx->my_lock.all) {
              // check the log for a RAW hazard, we expect to miss
              WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
              bool found = tx->writes.find(log);
              REDO_RAW_CHECK(found, log, mask);
              REDO_RAW_CLEANUP(tmp, found, log, mask);
              return tmp;
          }

          // abort if locked by other
          if (ivt.fields.lock)
              tmabort();

          // scale timestamp if ivt is too new
          uintptr_t newts = timestamp.val;
          OrecEagerRedoValidate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrecEagerRedo write (read-only context)
   *
   *    To write, put the value in the write buffer, then try to lock the orec.
   *
   *    NB: saving the value first decreases register pressure
   */
  void
  OrecEagerRedoWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec version number
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: uncontended location... lock it
          if (ivt.all <= tx->start_time) {
              if (!bcasptr(&o->v.all, ivt.all, tx->my_lock.all))
                  tmabort();

              // save old, log lock, write, return
              o->p = ivt.all;
              tx->locks.insert(o);
              OnFirstWrite(tx, OrecEagerRedoReadRW, OrecEagerRedoWriteRW, OrecEagerRedoCommitRW);
              return;
          }

          // fail if lock held
          if (ivt.fields.lock)
              tmabort();

          // unlocked but too new... scale forward and try again
          uintptr_t newts = timestamp.val;
          OrecEagerRedoValidate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrecEagerRedo write (writing context)
   *
   *    This is just like above, but with a condition for when the lock is held
   *    by the caller.
   */
  void
  OrecEagerRedoWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // read the orec version number
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: uncontended location... lock it
          if (ivt.all <= tx->start_time) {
              if (!bcasptr(&o->v.all, ivt.all, tx->my_lock.all))
                  tmabort();

              // save old, log lock, write, return
              o->p = ivt.all;
              tx->locks.insert(o);
              return;
          }

          // next best: already have the lock
          if (ivt.all == tx->my_lock.all)
              return;

          // fail if lock held
          if (ivt.fields.lock)
              tmabort();

          // unlocked but too new... scale forward and try again
          uintptr_t newts = timestamp.val;
          OrecEagerRedoValidate(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrecEagerRedo unwinder:
   *
   *    To unwind, we must release locks, but we don't have an undo log to run.
   */
  void
  OrecEagerRedoRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // release the locks and restore version numbers
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = (*i)->p;

      // undo memory operations, reset lists
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      PostRollback(tx);
      ResetToRO(tx, OrecEagerRedoReadRO, OrecEagerRedoWriteRO, OrecEagerRedoCommitRO);
  }

  /**
   *  OrecEagerRedo in-flight irrevocability: use abort-and-restart.
   */
  bool
  OrecEagerRedoIrrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  OrecEagerRedo validation
   *
   *    validate the read set by making sure that all orecs that we've read have
   *    timestamps older than our start time, unless we locked those orecs.
   */
  void
  OrecEagerRedoValidate(TxThread* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          // read this orec
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
              tmabort();
      }
  }

  /**
   *  Switch to OrecEagerRedo:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   */
  void
  OrecEagerRedoOnSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
  }

  /**
   *  OrecEagerRedo initialization
   */
  template<>
  void initTM<OrecEagerRedo>()
  {
      // set the name
      stms[OrecEagerRedo].name      = "OrecEagerRedo";

      // set the pointers
      stms[OrecEagerRedo].begin     = OrecEagerRedoBegin;
      stms[OrecEagerRedo].commit    = OrecEagerRedoCommitRO;
      stms[OrecEagerRedo].read      = OrecEagerRedoReadRO;
      stms[OrecEagerRedo].write     = OrecEagerRedoWriteRO;
      stms[OrecEagerRedo].rollback  = OrecEagerRedoRollback;
      stms[OrecEagerRedo].irrevoc   = OrecEagerRedoIrrevoc;
      stms[OrecEagerRedo].switcher  = OrecEagerRedoOnSwitchTo;
      stms[OrecEagerRedo].privatization_safe = false;
  }
}

#ifdef STM_ONESHOT_ALG_OrecEagerRedo
DECLARE_AS_ONESHOT_NORMAL(OrecEagerRedo)
#endif
