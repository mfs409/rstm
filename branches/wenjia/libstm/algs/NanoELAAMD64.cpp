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
 *  NanoELA_amd64 Implementation:
 *
 *    This STM is a surprising step backwards from the sorts of algorithms we
 *    are used to.  It accepts quadratic validation overhead, and eschews any
 *    timestamps.  It also has a limited set of Orecs.
 *
 *    The justification for this STM is two-fold.  First, it should not fare
 *    badly on multi-chip machines, since it lacks any bottlenecks.  Second, it
 *    should not fare badly on small transactions, despite the quadratic
 *    overhead.
 *
 *    This variant is privatization-safe.  The trick is that quadratic
 *    validation means we don't have a doomed transaction problem: this
 *    thread can't go on reading stuff that has been changed, since it
 *    validates its whole read set on every read anyway... it's like polling
 *    for conflicts, only more conservative.  So then all we need to do is
 *    prevent the delayed cleanup problem.  To do that, in this code, we use
 *    the Menon Epoch algorithm, but by using tick(TX_LONE_PARAMETER), we have a coherent
 *    clock for free.
 */

#include "algs.hpp"

namespace stm
{
  TM_FASTCALL void* NanoELAAMD64ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* NanoELAAMD64ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void NanoELAAMD64WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void NanoELAAMD64WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void NanoELAAMD64CommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void NanoELAAMD64CommitRW(TX_LONE_PARAMETER);

  /**
   *  NanoELAAMD64 begin:
   */
  void NanoELAAMD64Begin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
  }

  /**
   *  NanoELAAMD64 commit (read-only context)
   */
  void
  NanoELAAMD64CommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // read-only, so reset the orec list and we are done
      tx->nanorecs.reset();
      OnROCommit(tx);
  }

  /**
   *  NanoELAAMD64 commit (writing context)
   *
   *    There are no optimization opportunities here... we grab all locks,
   *    then validate, then do writeback.
   */
  void
  NanoELAAMD64CommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // as per Menon SPAA 2008, we need to start by updating our
      // linearization time
      uint64_t mynum = tickp();
      tx->last_val_time = mynum;
      __sync_add_and_fetch(&tx->last_val_time, 0);
      CFENCE;

      // acquire locks
      foreach (WriteSet, i, tx->writes) {
          // get orec, read its version#
          orec_t* o = get_nanorec(i->addr);
          id_version_t ivt;
          ivt.all = o->v.all;

          // if unlocked and we can lock it, do so
          if (ivt.all != tx->my_lock.all) {
              if (!ivt.fields.lock) {
                  if (!bcasptr(&o->v.all, ivt.all, tx->my_lock.all)) {
                      tx->last_val_time = (uint64_t)-1; // come out of epoch
                      tmabort();
                  }
                  // save old version to o->p, remember that we hold the lock
                  o->p = ivt.all;
                  tx->locks.insert(o);
              }
              else {
                  tx->last_val_time = (uint64_t)-1; // come out of epoch
                  tmabort();
              }
          }
      }

      // validate (variant for when locks are held)
      foreach (NanorecList, i, tx->nanorecs) {
          uintptr_t ivt = i->o->v.all;
          // if orec does not match val, then it must be locked by me, with its
          // old val equalling my expected val
          if ((ivt != i->v) && ((ivt != tx->my_lock.all) || (i->v != i->o->p)))
          {
              tx->last_val_time = (uint64_t)-1; // come out of epoch
              tmabort();
          }
      }

      // run the redo log
      tx->writes.writeback();

      tx->last_val_time = (uint64_t)-1; // come out of epoch

      // release locks
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = (*i)->p+1;

      // quiesce
      for (uint32_t id = 0; id < threadcount.val; ++id)
          while (threads[id]->last_val_time < mynum) spin64();

      // clean-up
      tx->nanorecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      OnRWCommit(tx);
      ResetToRO(tx, NanoELAAMD64ReadRO, NanoELAAMD64WriteRO, NanoELAAMD64CommitRO);
  }

  /**
   *  NanoELAAMD64 read (read-only context):
   */
  void*
  NanoELAAMD64ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // NanoELAAMD64 knows that it isn't a good algorithm when the read set is
      // large.  To address this situation, on every read, NanoELAAMD64 checks if the
      // transaction is too big, and if so, it sets a flag and aborts itself,
      // so that we can change algorithms.
      //
      // One danger is that we must have some sort of adaptivity policy in
      // place for this to work.  Implicit is that the adaptivity policy
      // can't continuously re-select NanoELAAMD64, but that's a problem for the
      // policy, not for this code.  This code need only ensure that it
      // doesn't self-abort unless there is an adaptive policy that will
      // register the trigger and cause a policy change.
      //
      // A hack here is that we use an extremely large consec_aborts rate to
      // indicate that NanoELAAMD64 is in big trouble.  So if this code cranks the
      // consec_aborts field up, then the trigger will assume that this is a
      // self-abort for the sake of switching, and will inform the adaptivity
      // policy accordingly.
      //
      // [mfs] note that the toxic transaction work suggests that 1024 aborts
      //       might happen anyway, so we may have a problem.  We're not
      //       going to worry about it for now.
      if (curr_policy.POL_ID != Single) {
          if (tx->nanorecs.size() > 8) {
              tx->consec_aborts = 1024;
              tmabort();
          }
      }

      // get the orec addr
      orec_t* o = get_nanorec(addr);

      while (true) {
          // read orec
          id_version_t ivt;
          ivt.all = o->v.all;
          CFENCE;

          // read the location
          void* tmp = *addr;
          CFENCE;

          // re-read orec
          uintptr_t ivt2 = o->v.all;

          // common case: valid read
          if ((ivt.all == ivt2) && (!ivt.fields.lock)) {
              // log the read
              tx->nanorecs.insert(nanorec_t(o, ivt2));
              // validate the whole read set, then return the value we just read
              foreach (NanorecList, i, tx->nanorecs)
                  if (i->o->v.all != i->v)
                      tmabort();
              return tmp;
          }

          // if lock held, spin before retrying
          if (o->v.fields.lock)
              spin64();
      }
  }

  /**
   *  NanoELAAMD64 read (writing context):
   */
  void*
  NanoELAAMD64ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse the ReadRO barrier, which is adequate here---reduces LOC
      void* val = NanoELAAMD64ReadRO(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  NanoELAAMD64 write (read-only context):
   */
  void
  NanoELAAMD64WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, NanoELAAMD64ReadRW, NanoELAAMD64WriteRW, NanoELAAMD64CommitRW);
  }

  /**
   *  NanoELAAMD64 write (writing context):
   */
  void
  NanoELAAMD64WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  NanoELAAMD64 unwinder:
   *
   *    Release any locks we acquired (if we aborted during a commit(TX_LONE_PARAMETER)
   *    operation), and then reset local lists.
   */
  void
  NanoELAAMD64Rollback(STM_ROLLBACK_SIG(tx, except, len))
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
      tx->nanorecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      PostRollback(tx);
      ResetToRO(tx, NanoELAAMD64ReadRO, NanoELAAMD64WriteRO, NanoELAAMD64CommitRO);
  }

  /**
   *  NanoELAAMD64 in-flight irrevocability:
   */
  bool NanoELAAMD64Irrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  Switch to NanoELAAMD64:
   *
   *    Since NanoELAAMD64 does not use timestamps, it can't use the regular
   *    orecs, or else switching would get nasty... that means that we don't
   *    need to do anything here.
   */
  void NanoELAAMD64OnSwitchTo() { }
}

DECLARE_SIMPLE_METHODS_FROM_NORMAL(NanoELAAMD64)
REGISTER_FGADAPT_ALG(NanoELAAMD64, "NanoELAAMD64", true)

#ifdef STM_ONESHOT_ALG_NanoELAAMD64
DECLARE_AS_ONESHOT(NanoELAAMD64)
#endif
