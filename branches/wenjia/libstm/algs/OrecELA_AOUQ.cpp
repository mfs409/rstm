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
 *  OrecELA_AOUQ Implementation: A variant of OrecELA in which AOU is used
 *  for low-overhead polling to prevent the doomed transaction problem, and
 *  commit-time quiescence of writers is used to prevent the delayed cleanup
 *  problem.
 */

#include "algs.hpp"
#ifdef STM_HAS_AOU
#include "../../include/asf-highlevel.h"  // For unlikely / likely macros
#else
#include "../Diagnostics.hpp"
#endif

namespace stm
{
  TM_FASTCALL void* OrecELA_AOUQReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* OrecELA_AOUQReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void OrecELA_AOUQWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void OrecELA_AOUQWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void OrecELA_AOUQCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void OrecELA_AOUQCommitRW(TX_LONE_PARAMETER);

#ifndef STM_HAS_AOU
  #define  Watch_Descriptor void
  #define likely
  #define AOU_stop(x)
  #define AOU_reset(x)
#endif

  void OrecELA_AOUQ_alloc_callback()
  {
      Self->aou_context->notify((void*)0xdead, Self->aou_context);
  }
  // [mfs] If I understand the AOU spec implementation correctly, this is
  //       what we use as the handler on an AOU alert
  NOINLINE void OrecELA_AOUQ_Handler(void* arg, Watch_Descriptor* w)
  {
#ifdef STM_HAS_AOU // NB: we'll crash at run-time before reaching this elided
                   // code if the program tries to use OrecELA_AOU without
                   // ASF support
      // [mfs] This isn't sufficient if we aren't using the default TLS
      //       access mechanism:
      TX_GET_TX_INTERNAL;

      uintptr_t ts = timestamp.val;
      w->locs[0].val = (uint64_t)ts;    // Update the expected value

      // ignore alert if we're in the midst of a library call... note that we
      // still will end up turning AOU back on in the caller... that's OK, we
      // just don't want to abort if suspend_aou is true... we'll call
      // OrecELA_AOUQ_Handler again later
      if unlikely (tx->suspend_aou) {
          tx->swallowed_aou = true;
          return;
      }

      // Similar method for checking whether the application tx allocator is
      // live.  Check its DND flag, and if set, register a callback.
      if unlikely (tx->allocator.getDND()) {
          tx->allocator.requestDNDCallback(&OrecELA_AOUQ_alloc_callback);
          return;
      }

      // If we just took an AOU alert, and are in this code, then we need to
      // decide whether we can keep running.  This basically just means we
      // need to validate...

      // optimized validation since we don't hold any locks
      tx->r_orecs.from_local_mem();
      foreach (OrecList, i, tx->r_orecs) {
          // if orec locked or newer than start time, abort
          if ((*i)->v.all > tx->start_time) {
              // corner case: if we used aou suppression (e.g., in tx_alloc),
              // then we might actually have live AOU here.  If so, shut off
              // AOU before aborting
              if (0xdead == (uintptr_t)arg)
                  AOU_stop(tx->aou_context);
              // NB: we aren't in an AOU context, so it is safe to abort here
              // without dropping AOU lines.  However, we need to reset our
              // AOU context
              AOU_reset(tx->aou_context);
              tmabort();
          }
      }

      tx->start_time = ts;
#else
      // hack to prevent -Werror issues at compile time
      uintptr_t a = (uintptr_t)arg;
      uintptr_t b = (uintptr_t)w;
      a += b;
#endif
  }

  /**
   *  OrecELA_AOUQ begin:
   *
   *    We need a starting point for the transaction.  If an in-flight
   *    transaction is committed, but still doing writeback, we can either start
   *    at the point where that transaction had not yet committed, or else we can
   *    wait for it to finish writeback.  In this code, we choose the former
   *    option.
   */
  void OrecELA_AOUQBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();

#ifdef STM_HAS_AOU // NB: we'll crash at run-time before reaching this elided
                   // code if the program tries to use OrecELA_AOU without
                   // ASF support
      // set up AOU context for every thread if it doesn't have one already...
      //
      // [mfs] This is not the optimal placement for this code, but will do
      //       for now
      if (__builtin_expect(!tx->aou_context, false)) {
          tx->aou_context = AOU_init(OrecELA_AOUQ_Handler, NULL, /*max_locs = */ 1);
          if (tx->aou_context == NULL)
              printf("Uh-Oh, context is null\n");
      }

      // turn on AOU tracking support
      AOU_start(tx->aou_context);

      // track the timestamp
      tx->start_time = AOU_load(tx->aou_context, (uint64_t*)&timestamp.val);
#endif
      // prepare CM
      BackoffCM::onBegin(tx);
  }

  /**
   *  OrecELA_AOUQ commit (read-only):
   *
   *    RO commit is trivial
   */
  void
  OrecELA_AOUQCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;

      // stop AOU tracking...
      AOU_stop(tx->aou_context);
      AOU_reset(tx->aou_context);

      // announce that I'm done
#ifdef STM_BITS_32
      tx->end_time = 0x7FFFFFFF;
#else
      tx->end_time = 0x7FFFFFFFFFFFFFFFLL;
#endif

      // clear cm
      BackoffCM::onCommit(tx);

      // standard RO commit stuff...
      tx->r_orecs.reset();
      OnROCommit(tx);
  }

  /**
   *  OrecELA_AOUQ commit (writing context):
   *
   *    OrecELA_AOUQ commit is like LLT: we get the locks, increment the counter, and
   *    then validate and do writeback.  As in other systems, some increments
   *    lead to skipping validation.
   *
   *    After writeback, we use a second, trailing counter to know when all txns
   *    who incremented the counter before this tx are done with writeback.  Only
   *    then can this txn mark its writeback complete.
   */
  void
  OrecELA_AOUQCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;

      // stop AOU tracking...
      AOU_stop(tx->aou_context);
      AOU_reset(tx->aou_context);

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
      uintptr_t end_time  = 1 + faiptr(&timestamp.val);

      // for quiescence
      //
      // [mfs] See note in OrecELAPQ... I am not trusting of the end_time
      // code, but what we're doing is safe.
      tx->end_time = end_time;
      CFENCE;

      // skip validation if possible
      if (tx->end_time != (tx->start_time + 1)) {
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

      // clear CM
      BackoffCM::onCommit(tx);

      // clean-up
      tx->r_orecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      OnRWCommit(tx);
      ResetToRO(tx, OrecELA_AOUQReadRO, OrecELA_AOUQWriteRO, OrecELA_AOUQCommitRO);

      // quiesce
      CFENCE;
      for (uint32_t id = 0; id < threadcount.val; ++id)
          while (threads[id]->end_time < end_time)
              spin64();
  }

  /**
   *  OrecELA_AOUQ read (read-only transaction)
   *
   *    This is a traditional orec read for systems with extendable timestamps.
   *    However, we also poll the timestamp counter and validate any time a new
   *    transaction has committed, in order to catch doomed transactions.
   */
  void*
  OrecELA_AOUQReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      // get the orec addr, read the orec's version#
      orec_t* o = get_orec(addr);
      if likely (tx->r_orecs.space() > 1) {
          tx->r_orecs.insert(o);
          tx->r_orecs.to_local_mem();
      }
      else {
#ifdef STM_HAS_AOU // NB: we'll crash at run-time before reaching this elided
           // code if the program tries to use OrecELA_AOU without
           // ASF support

          // turn AOU off so that we do not abort inside the resize operation
          tx->suspend_aou = true;
          CFENCE;
          tx->r_orecs.insert(o);
          CFENCE;
          // turn AOU back on
          Self->suspend_aou = false;
          CFENCE;
          if unlikely (Self->swallowed_aou) {
              // ok, clear the swallow flag and call notify.  There's just one
              // catch... AOU is ON right now.  Use a non-NULL arg to share that
              // info with the handler
              Self->swallowed_aou = false;
              Self->aou_context->notify((void*)0xdead, Self->aou_context);
          }
#endif
      }
      while (true) {
          // prevalidation
          id_version_t ivt;
          ivt.all = o->v.all;
          asm volatile ("":"=m"(*addr):"g"(ivt.all));

          // read the location
          void* tmp = *addr;
          asm volatile ("":"=m"(o->v.all):"g"(tmp));

          // check the orec.  Note: we don't need prevalidation because we
          // have a global clean state via the last_complete.val field.
          id_version_t ivt2;
          ivt2.all = o->v.all;

          // common case: new read to uncontended location
          if likely ((ivt.all == ivt2.all) && (ivt.all <= tx->start_time)) {
              // [mfs] Note that we don't have a privtest call, since we are using AOU
              return tmp;
          }

          // if lock held, spin and retry
          if (ivt.fields.lock) {
              spin64();
              continue;
          }

          // unlocked but too new... validate and scale forward
          //
          // [mfs] If we are aou tracking timestamp.val, is this code even
          //       possible?  I think not, but I'm not ready to test it
          //       because I don't know if the other AOU stuff is right
          //       yet...
          uintptr_t newts = timestamp.val;
          foreach_but_last (OrecList, i, tx->r_orecs) {
              // if orec locked or newer than start time, abort
              if ((*i)->v.all > tx->start_time) {
                  // stop AOU tracking...
                  AOU_stop(tx->aou_context);
                  AOU_reset(tx->aou_context);
                  // now we can abort, knowing that we're in a safe state in
                  // the abort handler
                  tmabort();
              }
          }

          // update start time if the validation was OK
          tx->start_time = newts;
      }
  }

  /**
   *  OrecELA_AOUQ read (writing transaction)
   *
   *    Identical to RO case, but with write-set lookup first
   */
  void*
  OrecELA_AOUQReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // reuse the ReadRO barrier, which is adequate here---reduces LOC
      void* val = OrecELA_AOUQReadRO(TX_FIRST_ARG addr STM_MASK(mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  OrecELA_AOUQ write (read-only context)
   *
   *    Simply buffer the write and switch to a writing context
   */
  void
  OrecELA_AOUQWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      if likely (!tx->writes.will_reorg())
          tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      else {
#ifdef STM_HAS_AOU // NB: we'll crash at run-time before reaching this elided
                   // code if the program tries to use OrecELA_AOU without
                   // ASF support

          // turn AOU off so that we do not abort inside the resize operation
          tx->suspend_aou = true;
          CFENCE;
          tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
          CFENCE;
          // turn AOU back on
          Self->suspend_aou = false;
          CFENCE;
          if unlikely (Self->swallowed_aou) {
              // ok, clear the swallow flag and call notify.  There's just one
              // catch... AOU is ON right now.  Use a non-NULL arg to share that
              // info with the handler
              Self->swallowed_aou = false;
              Self->aou_context->notify((void*)0xdead, Self->aou_context);
          }
#endif
      }
      OnFirstWrite(tx, OrecELA_AOUQReadRW, OrecELA_AOUQWriteRW, OrecELA_AOUQCommitRW);
  }

  /**
   *  OrecELA_AOUQ write (writing context)
   *
   *    Simply buffer the write
   */
  void
  OrecELA_AOUQWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      if likely (!tx->writes.will_reorg())
          tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      else {
#ifdef STM_HAS_AOU // NB: we'll crash at run-time before reaching this elided
                   // code if the program tries to use OrecELA_AOU without
                   // ASF support
          // turn AOU off so that we do not abort inside the resize operation
          tx->suspend_aou = true;
          CFENCE;
          tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
          CFENCE;
          // turn AOU back on
          Self->suspend_aou = false;
          CFENCE;
          if unlikely (Self->swallowed_aou) {
              // ok, clear the swallow flag and call notify.  There's just one
              // catch... AOU is ON right now.  Use a non-NULL arg to share that
              // info with the handler
              Self->swallowed_aou = false;
              Self->aou_context->notify((void*)0xdead, Self->aou_context);
          }
#endif
      }
  }

  /**
   *  OrecELA_AOUQ unwinder:
   *
   *    This is a standard orec unwind function.  The only catch is that if a
   *    transaction aborted after incrementing the timestamp, it must wait its
   *    turn and then increment the trailing timestamp, to keep the two counters
   *    consistent.
   */
  void
  OrecELA_AOUQRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      // announce that I'm done
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

      // notify CM
      BackoffCM::onAbort(tx);

      PostRollback(tx);
      ResetToRO(tx, OrecELA_AOUQReadRO, OrecELA_AOUQWriteRO, OrecELA_AOUQCommitRO);
  }

  /**
   *  OrecELA_AOUQ in-flight irrevocability: use abort-and-restart
   */
  bool OrecELA_AOUQIrrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  OrecELA_AOUQ validation
   *
   *    an in-flight transaction must make sure it isn't suffering from the
   *    "doomed transaction" half of the privatization problem.  We can get that
   *    effect by calling this after every transactional read (actually every
   *    read that detects that some new transaction has committed).
   *
   *  NB: this is dead code.
   */
  void
  OrecELA_AOUQPrivtest(TxThread* tx, uintptr_t ts)
  {
      // optimized validation since we don't hold any locks
      foreach (OrecList, i, tx->r_orecs) {
          // if orec locked or newer than start time, abort
          if ((*i)->v.all > tx->start_time) {
              // NB: we aren't in an AOU context, so it is safe to abort here
              // without dropping AOU lines.  However, we need to reset our
              // AOU context
              AOU_reset(tx->aou_context);
              tmabort();
          }
      }
      // careful here: we can't scale the start time past last_complete.val,
      // unless we want to re-introduce the need for prevalidation on every
      // read.
      uintptr_t cs = last_complete.val;
      tx->start_time = (ts < cs) ? ts : cs;
  }

  /**
   *  Switch to OrecELA_AOUQ:
   *
   *    The timestamp must be >= the maximum value of any orec.  Some algs use
   *    timestamp as a zero-one mutex.  If they do, then they back up the
   *    timestamp first, in timestamp_max.
   *
   *    Also, last_complete must equal timestamp
   */
  void
  OrecELA_AOUQOnSwitchTo()
  {
#ifndef STM_HAS_AOU
      UNRECOVERABLE("Cannot use OrecELA_AOUQ in a non-asf environment!");
#endif

      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
      for (uint32_t id = 0; id < threadcount.val; ++id) {
#ifdef STM_BITS_32
          threads[id]->end_time = 0x7FFFFFFF;
#else
          threads[id]->end_time = 0x7FFFFFFFFFFFFFFFLL;
#endif
      }  }
}

DECLARE_SIMPLE_METHODS_FROM_NORMAL(OrecELA_AOUQ)
REGISTER_FGADAPT_ALG(OrecELA_AOUQ, "OrecELA_AOUQ", true)

#ifdef STM_ONESHOT_ALG_OrecELA_AOUQ
DECLARE_AS_ONESHOT(OrecELA_AOUQ)
#endif
