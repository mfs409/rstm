/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

// tick instead of timestamp, no timestamp scaling, and wang-style
// timestamps... this should be pretty good

/**
 *  OrecELAAMD64 Implementation:
 *
 *    This STM is similar to OrecELA, with three exceptions.  First, we use
 *    the x86 tick counter in place of a shared memory counter, which lets us
 *    avoid a bottleneck when committing small writers.  Second, we solve the
 *    "doomed transaction" half of the privatization problem by using a
 *    validation fence, instead of by using polling on the counter.  Third,
 *    we use that same validation fence to address delayed cleanup, instead
 *    of using an ticket counter.
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  TM_FASTCALL void* OrecELAAMD64ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* OrecELAAMD64ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void OrecELAAMD64WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void OrecELAAMD64WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void OrecELAAMD64CommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void OrecELAAMD64CommitRW(TX_LONE_PARAMETER);
  NOINLINE void OrecELAAMD64Validate(TxThread*);

  /**
   *  OrecELAAMD64 begin:
   *
   *    Sample the timestamp and prepare local vars
   */
  void OrecELAAMD64Begin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
      tx->start_time = tickp() & 0x7FFFFFFFFFFFFFFFLL;
  }

  /**
   *  OrecELAAMD64 commit (read-only context)
   *
   *    We just reset local fields and we're done
   */
  void
  OrecELAAMD64CommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // read-only
      tx->r_orecs.reset();
      OnROCommit(tx);
#ifdef STM_BITS_32
      UNRECOVERABLE("Error: trying to run in 32-bit mode!");
#else
      tx->start_time = 0x7FFFFFFFFFFFFFFFLL;
#endif
  }

  /**
   *  OrecELAAMD64 commit (writing context)
   *
   *    Using Wang-style timestamps, we grab all locks, validate, writeback,
   *    increment the timestamp, and then release all locks.
   */
  void
  OrecELAAMD64CommitRW(TX_LONE_PARAMETER)
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

      // validate
      foreach (OrecList, i, tx->r_orecs) {
          uintptr_t ivt = (*i)->v.all;
          // if unlocked and newer than start time, abort
          if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
              tmabort();
      }

      // run the redo log
      tx->writes.writeback();

      // increment the global timestamp, release locks
      WBR; // for extremely small transactions, we're getting errors wrt the
           // timing of this tick... a WBR seems to resolve, though I don't
           // know why... tickp should be precise enough...
      CFENCE;
      uintptr_t end_time = tickp() & 0x7FFFFFFFFFFFFFFFLL;
      CFENCE;

      // announce that I'm done
#ifdef STM_BITS_32
      UNRECOVERABLE("Error: attempting to run 64-bit algorithm in 32-bit code.");
#else
      tx->start_time = 0x7FFFFFFFFFFFFFFFLL;
#endif

      // release locks
      foreach (OrecList, i, tx->locks)
          (*i)->v.all = end_time;

      // clean-up
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      OnRWCommit(tx);
      ResetToRO(tx, OrecELAAMD64ReadRO, OrecELAAMD64WriteRO, OrecELAAMD64CommitRO);

      // quiesce
      CFENCE;
      for (uint32_t id = 0; id < threadcount.val; ++id)
	while (threads[id]->start_time < end_time)
	  spin64();
}

  /**
   *  OrecELAAMD64 read (read-only context):
   *
   *    in the best case, we just read the value, check the timestamp, log the
   *    orec and return
   */
  void* OrecELAAMD64ReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // get the orec addr
      orec_t* o = get_orec(addr);

      while (true) {
          // read the location
          void* tmp = *addr;
          CFENCE;
          //  check the orec.
          //  NB: with this variant of timestamp, we don't need prevalidation
          id_version_t ivt;
          ivt.all = o->v.all;

          // common case: new read to uncontended location
          if (ivt.all <= tx->start_time) {
              tx->r_orecs.insert(o);
              return tmp;
          }

          // if lock held, spin and retry
          if (ivt.fields.lock) {
              spin64();
              continue;
          }

          // scale timestamp if ivt is too new, then try again
          CFENCE;
          uint64_t newts = tickp() & 0x7FFFFFFFFFFFFFFFLL;
          CFENCE;
          OrecELAAMD64Validate(tx);
	  CFENCE;
          tx->start_time = newts;
      }
  }

  /**
   *  OrecELAAMD64 read (writing context):
   *
   *    Just like read-only context, but must check the write set first
   */
  void*
  OrecELAAMD64ReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse the ReadRO barrier, which is adequate here---reduces LOC
      void* val = OrecELAAMD64ReadRO(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  OrecELAAMD64 write (read-only context):
   *
   *    Buffer the write, and switch to a writing context
   */
  void
  OrecELAAMD64WriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, OrecELAAMD64ReadRW, OrecELAAMD64WriteRW, OrecELAAMD64CommitRW);
  }

  /**
   *  OrecELAAMD64 write (writing context):
   *
   *    Just buffer the write
   */
  void
  OrecELAAMD64WriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  OrecELAAMD64 rollback:
   *
   *    Release any locks we acquired (if we aborted during a commit(TX_LONE_PARAMETER)
   *    operation), and then reset local lists.
   */
  void
  OrecELAAMD64Rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      tx->start_time = 0x7FFFFFFFFFFFFFFFLL;
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
      ResetToRO(tx, OrecELAAMD64ReadRO, OrecELAAMD64WriteRO, OrecELAAMD64CommitRO);
  }

  /**
   *  OrecELAAMD64 in-flight irrevocability:
   *
   *    Either commit the transaction or return false.
   */
   bool OrecELAAMD64Irrevoc(TxThread*)
   {
       return false;
       // NB: In a prior release, we actually had a full OrecELAAMD64 commit
       //     here.  Any contributor who is interested in improving this code
       //     should note that such an approach is overkill: by the time this
       //     runs, there are no concurrent transactions, so in effect, all
       //     that is needed is to validate, writeback, and return true.
   }

  /**
   *  OrecELAAMD64 validation:
   *
   *    We only call this when in-flight, which means that we don't have any
   *    locks... This makes the code very simple, but it is still better to not
   *    inline it.
   */
  void OrecELAAMD64Validate(TxThread* tx)
  {
      foreach (OrecList, i, tx->r_orecs)
          // abort if orec locked, or if unlocked but timestamp too new
          if ((*i)->v.all > tx->start_time)
              tmabort();
  }

  /**
   *  Switch to OrecELAAMD64:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   */
  void OrecELAAMD64OnSwitchTo()
  {
  }
}


DECLARE_SIMPLE_METHODS_FROM_NORMAL(OrecELAAMD64)
REGISTER_FGADAPT_ALG(OrecELAAMD64, "OrecELAAMD64", true)

#ifdef STM_ONESHOT_ALG_OrecELAAMD64
DECLARE_AS_ONESHOT(OrecELAAMD64)
#endif
