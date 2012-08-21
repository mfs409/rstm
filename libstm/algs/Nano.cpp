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
 *  Nano Implementation:
 *
 *    This STM is a surprising step backwards from the sorts of algorithms we
 *    are used to.  It accepts quadratic validation overhead, and eschews any
 *    timestamps.  It also has a limited set of Orecs.
 *
 *    The justification for this STM is two-fold.  First, it should not fare
 *    badly on multi-chip machines, since it lacks any bottlenecks.  Second, it
 *    should not fare badly on small transactions, despite the quadratic
 *    overhead.
 */

#include "algs.hpp"

namespace stm
{
  TM_FASTCALL void* NanoReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* NanoReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void NanoWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void NanoWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void NanoCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void NanoCommitRW(TX_LONE_PARAMETER);

  /**
   *  Nano begin:
   */
  void NanoBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
  }

  /**
   *  Nano commit (read-only context)
   */
  void
  NanoCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // read-only, so reset the orec list and we are done
      tx->nanorecs.reset();
      OnROCommit(tx);
  }

  /**
   *  Nano commit (writing context)
   *
   *    There are no optimization opportunities here... we grab all locks,
   *    then validate, then do writeback.
   */
  void
  NanoCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // acquire locks
      foreach (WriteSet, i, tx->writes) {
          // get orec, read its version#
          orec_t* o = get_nanorec(i->addr);
          id_version_t ivt;
          ivt.all = o->v.all;

          // if unlocked and we can lock it, do so
          if (ivt.all != tx->my_lock.all) {
              if (!ivt.fields.lock) {
                  if (!bcasptr(&o->v.all, ivt.all, tx->my_lock.all))
                      tmabort();
                  // save old version to o->p, remember that we hold the lock
                  o->p = ivt.all;
                  tx->locks.insert(o);
              }
              else {
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
              tmabort();
      }

      // run the redo log
      tx->writes.writeback();

      // release locks
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = (*i)->p+1;

      // clean-up
      tx->nanorecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      OnRWCommit(tx);
      ResetToRO(tx, NanoReadRO, NanoWriteRO, NanoCommitRO);
  }

  /**
   *  Nano read (read-only context):
   */
  void*
  NanoReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // Nano knows that it isn't a good algorithm when the read set is
      // large.  To address this situation, on every read, Nano checks if the
      // transaction is too big, and if so, it sets a flag and aborts itself,
      // so that we can change algorithms.
      //
      // One danger is that we must have some sort of adaptivity policy in
      // place for this to work.  Implicit is that the adaptivity policy
      // can't continuously re-select Nano, but that's a problem for the
      // policy, not for this code.  This code need only ensure that it
      // doesn't self-abort unless there is an adaptive policy that will
      // register the trigger and cause a policy change.
      //
      // A hack here is that we use an extremely large consec_aborts rate to
      // indicate that Nano is in big trouble.  So if this code cranks the
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
   *  Nano read (writing context):
   */
  void*
  NanoReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse the ReadRO barrier, which is adequate here---reduces LOC
      void* val = NanoReadRO(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  Nano write (read-only context):
   */
  void
  NanoWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, NanoReadRW, NanoWriteRW, NanoCommitRW);
  }

  /**
   *  Nano write (writing context):
   */
  void
  NanoWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  Nano unwinder:
   *
   *    Release any locks we acquired (if we aborted during a commit(TX_LONE_PARAMETER)
   *    operation), and then reset local lists.
   */
  void
  NanoRollback(STM_ROLLBACK_SIG(tx, except, len))
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
      ResetToRO(tx, NanoReadRO, NanoWriteRO, NanoCommitRO);
  }

  /**
   *  Nano in-flight irrevocability:
   */
  bool NanoIrrevoc(TxThread*) {
      return false;
  }

  /**
   *  Switch to Nano:
   *
   *    Since Nano does not use timestamps, it can't use the regular orecs, or
   *    else switching would get nasty... that means that we don't need to do
   *    anything here.
   */
  void NanoOnSwitchTo() {
  }
}

DECLARE_SIMPLE_METHODS_FROM_NORMAL(Nano)
REGISTER_FGADAPT_ALG(Nano, "Nano", false)

#ifdef STM_ONESHOT_ALG_Nano
DECLARE_AS_ONESHOT(Nano)
#endif
