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

#include "../profiling.hpp"
#include "algs.hpp"
#include "../RedoRAWUtils.hpp"

using stm::TxThread;
using stm::WriteSet;
using stm::WriteSetEntry;
using stm::OrecList;
using stm::orec_t;
using stm::NanorecList;
using stm::nanorec_t;
using stm::get_nanorec;
using stm::id_version_t;


/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace
{
  struct NanoELA_amd64
  {
      static void begin(TX_LONE_PARAMETER);
      static TM_FASTCALL void* ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void* ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void CommitRO(TX_LONE_PARAMETER);
      static TM_FASTCALL void CommitRW(TX_LONE_PARAMETER);

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
  };

  /**
   *  NanoELA_amd64 begin:
   */
  void NanoELA_amd64::begin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
  }

  /**
   *  NanoELA_amd64 commit (read-only context)
   */
  void
  NanoELA_amd64::CommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // read-only, so reset the orec list and we are done
      tx->nanorecs.reset();
      OnROCommit(tx);
  }

  /**
   *  NanoELA_amd64 commit (writing context)
   *
   *    There are no optimization opportunities here... we grab all locks,
   *    then validate, then do writeback.
   */
  void
  NanoELA_amd64::CommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // as per Menon SPAA 2008, we need to start by updating our
      // linearization time
      uint64_t mynum = tickp();
      tx->last_val_time = mynum;
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
                      stm::tmabort();
                  }
                  // save old version to o->p, remember that we hold the lock
                  o->p = ivt.all;
                  tx->locks.insert(o);
              }
              else {
                  tx->last_val_time = (uint64_t)-1; // come out of epoch
                  stm::tmabort();
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
              stm::tmabort();
          }
      }

      // run the redo log
      tx->writes.writeback();

      tx->last_val_time = (uint64_t)-1; // come out of epoch

      // release locks
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = (*i)->p+1;

      // quiesce
      for (uint32_t id = 0; id < stm::threadcount.val; ++id)
          while (stm::threads[id]->last_val_time < mynum) spin64();

      // clean-up
      tx->nanorecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      OnRWCommit(tx);
      ResetToRO(tx, ReadRO, WriteRO, CommitRO);
  }

  /**
   *  NanoELA_amd64 read (read-only context):
   */
  void*
  NanoELA_amd64::ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // NanoELA_amd64 knows that it isn't a good algorithm when the read set is
      // large.  To address this situation, on every read, NanoELA_amd64 checks if the
      // transaction is too big, and if so, it sets a flag and aborts itself,
      // so that we can change algorithms.
      //
      // One danger is that we must have some sort of adaptivity policy in
      // place for this to work.  Implicit is that the adaptivity policy
      // can't continuously re-select NanoELA_amd64, but that's a problem for the
      // policy, not for this code.  This code need only ensure that it
      // doesn't self-abort unless there is an adaptive policy that will
      // register the trigger and cause a policy change.
      //
      // A hack here is that we use an extremely large consec_aborts rate to
      // indicate that NanoELA_amd64 is in big trouble.  So if this code cranks the
      // consec_aborts field up, then the trigger will assume that this is a
      // self-abort for the sake of switching, and will inform the adaptivity
      // policy accordingly.
      //
      // [mfs] note that the toxic transaction work suggests that 1024 aborts
      //       might happen anyway, so we may have a problem.  We're not
      //       going to worry about it for now.
      if (stm::curr_policy.POL_ID != stm::Single) {
          if (tx->nanorecs.size() > 8) {
              tx->consec_aborts = 1024;
              stm::tmabort();
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
                      stm::tmabort();
              return tmp;
          }

          // if lock held, spin before retrying
          if (o->v.fields.lock)
              spin64();
      }
  }

  /**
   *  NanoELA_amd64 read (writing context):
   */
  void*
  NanoELA_amd64::ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse the ReadRO barrier, which is adequate here---reduces LOC
      void* val = ReadRO(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  NanoELA_amd64 write (read-only context):
   */
  void
  NanoELA_amd64::WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      stm::OnFirstWrite(tx, ReadRW, WriteRW, CommitRW);
  }

  /**
   *  NanoELA_amd64 write (writing context):
   */
  void
  NanoELA_amd64::WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  NanoELA_amd64 unwinder:
   *
   *    Release any locks we acquired (if we aborted during a commit(TX_LONE_PARAMETER)
   *    operation), and then reset local lists.
   */
  void
  NanoELA_amd64::rollback(STM_ROLLBACK_SIG(tx, except, len))
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
      ResetToRO(tx, ReadRO, WriteRO, CommitRO);
  }

  /**
   *  NanoELA_amd64 in-flight irrevocability:
   */
  bool NanoELA_amd64::irrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  Switch to NanoELA_amd64:
   *
   *    Since NanoELA_amd64 does not use timestamps, it can't use the regular
   *    orecs, or else switching would get nasty... that means that we don't
   *    need to do anything here.
   */
  void NanoELA_amd64::onSwitchTo()
  {
  }
}

namespace stm
{
  /**
   *  NanoELA_amd64 initialization
   */
  template<>
  void initTM<NanoELA_amd64>()
  {
      // set the name
      stms[NanoELA_amd64].name      = "NanoELA_amd64";

      // set the pointers
      stms[NanoELA_amd64].begin     = ::NanoELA_amd64::begin;
      stms[NanoELA_amd64].commit    = ::NanoELA_amd64::CommitRO;
      stms[NanoELA_amd64].read      = ::NanoELA_amd64::ReadRO;
      stms[NanoELA_amd64].write     = ::NanoELA_amd64::WriteRO;
      stms[NanoELA_amd64].rollback  = ::NanoELA_amd64::rollback;
      stms[NanoELA_amd64].irrevoc   = ::NanoELA_amd64::irrevoc;
      stms[NanoELA_amd64].switcher  = ::NanoELA_amd64::onSwitchTo;
      stms[NanoELA_amd64].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_NanoELA_amd64
DECLARE_AS_ONESHOT_NORMAL(NanoELA_amd64)
#endif
