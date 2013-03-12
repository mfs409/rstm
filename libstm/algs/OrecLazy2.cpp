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
 *  OrecLazy2 Implementation:
 *
 *    This STM is similar to the commit-time locking variant of TinySTM.  It
 *    also resembles the "patient" STM published by Spear et al. at PPoPP 2009.
 *    The key difference deals with the way timestamps are managed.  This code
 *    uses the manner of timestamps described by Wang et al. in their CGO 2007
 *    paper.  More details can be found in the OrecEager implementation.
 */

// TODO: switch to single-check orecs; de-templatize?


#include "../cm.hpp"
#include "algs.hpp"

namespace stm
{
  template <class CM>
  TM_FASTCALL void* OrecLazy2GenericReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  template <class CM>
  TM_FASTCALL void* OrecLazy2GenericReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  template <class CM>
  TM_FASTCALL void OrecLazy2GenericWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  template <class CM>
  TM_FASTCALL void OrecLazy2GenericWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  template <class CM>
  TM_FASTCALL void OrecLazy2GenericCommitRO(TX_LONE_PARAMETER);
  template <class CM>
  TM_FASTCALL void OrecLazy2GenericCommitRW(TX_LONE_PARAMETER);
  template <class CM>
  NOINLINE void OrecLazy2GenericValidate(TxThread*);

  /**
   *  OrecLazy2 begin:
   *
   *    Sample the timestamp and prepare local vars
   */
  template <class CM>
  void OrecLazy2GenericBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
      tx->start_time = timestamp.val;
      CM::onBegin(tx);
  }

  /**
   *  OrecLazy2 commit (read-only context)
   *
   *    We just reset local fields and we're done
   */
  template <class CM>
  TM_FASTCALL
  void
  OrecLazy2GenericCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // notify CM
      CM::onCommit(tx);
      // read-only
      tx->r_orecs.reset();
      OnROCommit(tx);
  }

  /**
   *  OrecLazy2 commit (writing context)
   *
   *    Using Wang-style timestamps, we grab all locks, validate, writeback,
   *    increment the timestamp, and then release all locks.
   */
  template <class CM>
  TM_FASTCALL
  void
  OrecLazy2GenericCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // acquire locks
      foreach (WriteSet, i, tx->writes) {
          // get orec, read its version#
          orec_t* o = get_orec(i->addr);
          uintptr_t ivt = o->v.all;

          // lock all orecs, unless already locked
          if (ivt <= tx->start_time) {
              // abort if cannot acquire
              if (!bcasptr(&o->v.all, ivt, tx->my_lock.all))
                  tmabort();
              // save old version to o->p, remember that we hold the lock
              o->p = ivt;
              tx->locks.insert(o);
          }
          // else if we don't hold the lock abort
          else if (ivt != tx->my_lock.all) {
              tmabort();
          }
      }

      // increment the global timestamp if we have writes
      uintptr_t end_time = 1 + faiptr(&timestamp.val);

      // skip validation if possible
      if (end_time != (tx->start_time + 1)) {
          foreach (OrecList, i, tx->r_orecs) {
              // read this orec
              uintptr_t ivt = (*i)->v.all;
              if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
                  tmabort();
          }
      }

      // run the redo log
      tx->writes.writeback();
      CFENCE;

      // release locks
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = end_time;

      // notify CM
      CM::onCommit(tx);

      // clean-up
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      OnRWCommit(tx);
      ResetToRO(tx, OrecLazy2GenericReadRO<CM>, OrecLazy2GenericWriteRO<CM>, OrecLazy2GenericCommitRO<CM>);
  }

  /**
   *  OrecLazy2 read (read-only context):
   *
   *    in the best case, we just read the value, check the timestamp, log the
   *    orec and return
   */
  template <class CM>
  TM_FASTCALL
  void*
  OrecLazy2GenericReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // get the orec addr
      orec_t* o = get_orec(addr);
      while (true) {
          // prevalidation
          id_version_t ivt;
          ivt.all = o->v.all;
          CFENCE;

          // read the location
          void* tmp = *addr;
          CFENCE;
          //  check the orec.
          //  NB: with this variant of timestamp, we don't need prevalidation
          id_version_t ivt2;
          ivt2.all = o->v.all;

          // common case: new read to uncontended location
          if ((ivt.all == ivt2.all) && (ivt.all <= tx->start_time)) {
              tx->r_orecs.insert(o);
              return tmp;
          }

          // if lock held, spin and retry
          if (ivt.fields.lock) {
              spin64();
              continue;
          }

          // scale timestamp if ivt is too new, then try again
          uintptr_t newts = timestamp.val;
          OrecLazy2GenericValidate<CM>(tx);
          tx->start_time = newts;
      }
  }

  /**
   *  OrecLazy2 read (writing context):
   *
   *    Just like read-only context, but must check the write set first
   */
  template <class CM>
  TM_FASTCALL
  void*
  OrecLazy2GenericReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse the ReadRO barrier, which is adequate here---reduces LOC
      void* val = OrecLazy2GenericReadRO<CM>(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  OrecLazy2 write (read-only context):
   *
   *    Buffer the write, and switch to a writing context
   */
  template <class CM>
  TM_FASTCALL
  void
  OrecLazy2GenericWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, OrecLazy2GenericReadRW<CM>, OrecLazy2GenericWriteRW<CM>, OrecLazy2GenericCommitRW<CM>);
  }

  /**
   *  OrecLazy2 write (writing context):
   *
   *    Just buffer the write
   */
  template <class CM>
  TM_FASTCALL
  void
  OrecLazy2GenericWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  OrecLazy2 rollback:
   *
   *    Release any locks we acquired (if we aborted during a commit(TX_LONE_PARAMETER)
   *    operation), and then reset local lists.
   */
  template <class CM>
  void OrecLazy2GenericRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // release the locks and restore version numbers
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = (*i)->p;

      // notify CM
      CM::onAbort(tx);

      // undo memory operations, reset lists
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      PostRollback(tx);
      ResetToRO(tx, OrecLazy2GenericReadRO<CM>, OrecLazy2GenericWriteRO<CM>, OrecLazy2GenericCommitRO<CM>);
  }

  /**
   *  OrecLazy2 in-flight irrevocability:
   *
   *    Either commit the transaction or return false.
   */
  template <class CM>
  bool OrecLazy2GenericIrrevoc(TxThread*)
  {
      return false;
      // NB: In a prior release, we actually had a full OrecLazy2 commit
      //     here.  Any contributor who is interested in improving this code
      //     should note that such an approach is overkill: by the time this
      //     runs, there are no concurrent transactions, so in effect, all
      //     that is needed is to validate, writeback, and return true.
  }

  /**
   *  OrecLazy2 validation:
   *
   *    We only call this when in-flight, which means that we don't have any
   *    locks... This makes the code very simple, but it is still better to not
   *    inline it.
   */
  template<class CM>
  void OrecLazy2GenericValidate(TxThread* tx)
  {
      foreach (OrecList, i, tx->r_orecs)
          // abort if orec locked, or if unlocked but timestamp too new
          if ((*i)->v.all > tx->start_time)
              tmabort();
  }

  /**
   *  Switch to OrecLazy2:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   */
  template <class CM>
  void OrecLazy2GenericOnSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
  }
}

DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(OrecLazy2, OrecLazy2, HyperAggressiveCM)
REGISTER_TEMPLATE_ALG(OrecLazy2, OrecLazy2, "OrecLazy2", false, HyperAggressiveCM)

#ifdef STM_ONESHOT_ALG_OrecLazy2
DECLARE_AS_ONESHOT(OrecLazy2)
#endif
