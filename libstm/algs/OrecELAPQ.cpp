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
 *  OrecELAPQ Implementation: A variant of OrecELA in which we poll to
 *  prevent doomed transactions, and we use quiescence at commit time
 *  (writers only) to prevent the delayed cleanup problem.
 */

#include "algs.hpp"

namespace stm
{
  TM_FASTCALL void* OrecELAPQReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* OrecELAPQReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void OrecELAPQWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void OrecELAPQWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void OrecELAPQCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void OrecELAPQCommitRW(TX_LONE_PARAMETER);

  /**
   *  OrecELAPQ begin:
   *
   *    We need a starting point for the transaction.  If an in-flight
   *    transaction is committed, but still doing writeback, we can either start
   *    at the point where that transaction had not yet committed, or else we can
   *    wait for it to finish writeback.  In this code, we choose the former
   *    option.
   */
  void OrecELAPQBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
      // Start after the last cleanup, instead of after the last commit, to
      // avoid spinning in Begin
      tx->start_time = timestamp.val;
  }

  /**
   *  OrecELAPQ commit (read-only):
   *
   *    RO commit is trivial
   */
  void
  OrecELAPQCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // announce that I'm done
      //
      // [mfs] this code is probably not needed, but I'm not 100% sure that
      // we have a good initial value of end_time, and dumping this here
      // means that every exit path for every transaction clears the end
      // time.  In the worst case, this means that in the worst case, someone
      // can wait in the commitRW function on a read-only transaction, but at
      // least they'll only do so once.
#ifdef STM_BITS_32
      tx->end_time = 0x7FFFFFFF;
#else
      tx->end_time = 0x7FFFFFFFFFFFFFFFLL;
#endif
      tx->r_orecs.reset();
      OnROCommit(tx);
  }

  /**
   *  OrecELAPQ commit (writing context):
   *
   *    OrecELAPQ commit is like LLT: we get the locks, increment the counter, and
   *    then validate and do writeback.  As in other systems, some increments
   *    lead to skipping validation.
   *
   *    After writeback, we use a second, trailing counter to know when all txns
   *    who incremented the counter before this tx are done with writeback.  Only
   *    then can this txn mark its writeback complete.
   */
  void
  OrecELAPQCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;

      // set a flag for quiescence
      tx->end_time = 0;
      CFENCE;

      // acquire locks
      foreach (WriteSet, i, tx->writes) {
          // get orec, read its version#
          orec_t* o = get_orec(i->addr);
          uintptr_t ivt = o->v.all;

          // if orec not locked, lock it and save old to orec.p
          if (ivt <= tx->start_time) {
              // abort if cannot acquire
              if (!bcasptr(&o->v.all, ivt, tx->my_lock.all))
                  tmabort();
              // save old version to o->p, log lock
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

      // for quiescence
      tx->end_time = end_time;
      CFENCE;

      // [mfs] Note that I don't trust this algorithm right now... there's
      //       something fishy about the increment not being atomic with the
      //       subsequent set of end_time.  There may be a subtle correctness
      //       argument that stems from something in the way that validations
      //       of concurrent algorithms work, but I haven't verified it.  For
      //       now, I'm using the extra end_time set at the beginning of this
      //       function to address my concern.

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

      // announce that I'm done
#ifdef STM_BITS_32
      tx->end_time = 0x7FFFFFFF;
#else
      tx->end_time = 0x7FFFFFFFFFFFFFFFLL;
#endif

      // release locks
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = end_time;
      CFENCE;

      // clean-up
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      OnRWCommit(tx);
      ResetToRO(tx, OrecELAPQReadRO, OrecELAPQWriteRO, OrecELAPQCommitRO);

      // quiesce
      CFENCE;
      for (uint32_t id = 0; id < threadcount.val; ++id)
          while (threads[id]->end_time < end_time)
              spin64();
  }

  /**
   *  OrecELAPQ read (read-only transaction)
   *
   *    This is a traditional orec read for systems with extendable timestamps.
   *    However, we also poll the timestamp counter and validate any time a new
   *    transaction has committed, in order to catch doomed transactions.
   */
  void*
  OrecELAPQReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // get the orec addr, read the orec's version#
      orec_t* o = get_orec(addr);
      while (true) {
          // prevalidation
          id_version_t ivt;
          ivt.all = o->v.all;
          CFENCE;

          // read the location
          void* tmp = *addr;
          CFENCE;

          // check the orec.  Note: we don't need prevalidation because we
          // have a global clean state via the last_complete.val field.
          id_version_t ivt2;
          ivt2.all = o->v.all;

          // common case: new read to uncontended location
          if ((ivt.all == ivt2.all) && (ivt.all <= tx->start_time)) {
              tx->r_orecs.insert(o);
              // privatization safety: avoid the "doomed transaction" half
              // of the privatization problem by polling a global and
              // validating if necessary
              uintptr_t ts = timestamp.val;
              if (ts != tx->start_time) {
                  foreach (OrecList, i, tx->r_orecs) {
                      // if orec locked or newer than start time, abort
                      if ((*i)->v.all > tx->start_time)
                          tmabort();
                  }
                  tx->start_time = ts;
              }
              return tmp;
          }

          // if lock held, spin and retry
          if (ivt.fields.lock) {
              spin64();
              continue;
          }

          // unlocked but too new... validate and scale forward
          uintptr_t newts = timestamp.val;
          foreach (OrecList, i, tx->r_orecs) {
              // if orec locked or newer than start time, abort
              if ((*i)->v.all > tx->start_time)
                  tmabort();
          }

          // [mfs] We could be a bit cheaper here wrt privatization if we
          //       updated start_time earlier
          tx->start_time = newts;
      }
  }

  /**
   *  OrecELAPQ read (writing transaction)
   *
   *    Identical to RO case, but with write-set lookup first
   */
  void*
  OrecELAPQReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse the ReadRO barrier, which is adequate here---reduces LOC
      void* val = OrecELAPQReadRO(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  OrecELAPQ write (read-only context)
   *
   *    Simply buffer the write and switch to a writing context
   */
  void
  OrecELAPQWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, OrecELAPQReadRW, OrecELAPQWriteRW, OrecELAPQCommitRW);
  }

  /**
   *  OrecELAPQ write (writing context)
   *
   *    Simply buffer the write
   */
  void
  OrecELAPQWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  OrecELAPQ unwinder:
   *
   *    This is a standard orec unwind function.  The only catch is that if a
   *    transaction aborted after incrementing the timestamp, it must wait its
   *    turn and then increment the trailing timestamp, to keep the two counters
   *    consistent.
   */
  void
  OrecELAPQRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      // announce I'm done
#ifdef STM_BITS_32
      tx->end_time = 0x7FFFFFFF;
#else
      tx->end_time = 0x7FFFFFFFFFFFFFFFLL;
#endif

      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // release locks and restore version numbers
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = (*i)->p;
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      PostRollback(tx);
      ResetToRO(tx, OrecELAPQReadRO, OrecELAPQWriteRO, OrecELAPQCommitRO);
  }

  /**
   *  OrecELAPQ in-flight irrevocability: use abort-and-restart
   */
  bool OrecELAPQIrrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  Switch to OrecELAPQ:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   *    Also, last_complete must equal timestamp
   */
  void
  OrecELAPQOnSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      for (uint32_t id = 0; id < threadcount.val; ++id) {
#ifdef STM_BITS_32
          threads[id]->end_time = 0x7FFFFFFF;
#else
          threads[id]->end_time = 0x7FFFFFFFFFFFFFFFLL;
#endif
      }
  }
}

DECLARE_SIMPLE_METHODS_FROM_NORMAL(OrecELAPQ)
REGISTER_FGADAPT_ALG(OrecELAPQ, "OrecELAPQ", true)

#ifdef STM_ONESHOT_ALG_OrecELAPQ
DECLARE_AS_ONESHOT(OrecELAPQ)
#endif
