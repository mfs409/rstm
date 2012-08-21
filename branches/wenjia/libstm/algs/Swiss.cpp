/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "algs.hpp"
#include "../cm.hpp"

/**
 *  This is a good-faith implementation of SwissTM.
 *
 *  What that means, precisely, has to do with how we translate the SwissTM
 *  algorithm to allow /algorithmic/ comparisons with OrecEager and LLT.
 *  Specifically, we decided in the past that OrecEager and LLT would not use
 *  any of the clever 'lock is a pointer into my writeset' tricks that were
 *  proposed in the TinySTM paper, and so we don't use those tricks here,
 *  either.  The cost is minimal (actually, with the RSTM WriteSet hash, the
 *  tricks are typically not profitable anyway), but it is worth stating, up
 *  front, that we do not adhere to this design point.
 *
 *  Additionally, orec management differs slightly here from in OrecEager and
 *  LLT.  In those systems, we use "2-word" orecs, where the acquirer writes
 *  the old orec value in the second word after acquiring the first word.
 *  This halves the cost of logging, as the list of held locks only gives
 *  orec addresses, not the old values.  However, in SwissTM, there is a
 *  tradeoff where on one hand, having rlocks separate from wlocks can
 *  decrease cache misses for read-only transactions, but on the other hand
 *  doing so doubles logging overhead for read locking by writers at commit
 *  time.  It would be odd to use the 2-word orecs for read locks and not for
 *  write locks, but a more efficient technique is to use the second word of
 *  2-word orecs as the rlock, and then use traditional 2-word lock logging,
 *  where the old lock value is also stored.
 *
 *  Other changes are typically small.  The biggest deals with adding
 *  detection of remote aborts, which wasn't discussed in the paper.
 *
 *  NB: we could factor some CM code out of the RO codepath.  We could also
 *  make the phase2 switch cause a thread to use different function pointers.
 */

namespace stm
{
  TM_FASTCALL void* SwissRead(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void SwissWrite(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void SwissCommit(TX_LONE_PARAMETER);
  void SwissCMStart(TxThread*);
  void SwissCMOnRollback(TxThread*);
  void SwissCMOnWrite(TxThread*);
  bool SwissCMShouldAbort(TxThread*, uintptr_t owner_id);
  NOINLINE void SwissValidateInflight(TxThread*);
  NOINLINE void SwissValidateCommit(TxThread*);

  /**
   * begin swiss transaction: set to active, notify allocator, get start
   * time, and notify CM
   */
  void SwissBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->alive = ACTIVE;
      tx->allocator.onTxBegin();
      tx->start_time = timestamp.val;
      SwissCMStart(tx);
  }

  // word based transactional read
  void* SwissRead(TX_FIRST_PARAMETER STM_READ_SIG( addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // get orec address
      orec_t* o = get_orec(addr);

      // do I own the orec?
      if (o->v.all == tx->my_lock.all) {
          CFENCE; // order orec check before possible read of *addr
          // if this address is in my writeset, return looked-up value, else
          // do a direct read from memory
          WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
          bool found = tx->writes.find(log);
          REDO_RAW_CHECK(found, log, mask);

          void* val = *addr;
          REDO_RAW_CLEANUP(val, found, log, mask);
          return val;
      }

      while (true) {
          // get a consistent read of the value, during a period where the
          // read version is unchanging and not locked
          uintptr_t rver1 = o->p;
          CFENCE;
          void* tmp = *addr;
          CFENCE;
          uintptr_t rver2 = o->p;
          // deal with inconsistent reads
          if ((rver1 != rver2) || (rver1 == UINT_MAX)) {
              // bad read: we'll go back to top, but first make sure we didn't
              // get aborted
              if (tx->alive == ABORTED)
                  tmabort();
              continue;
          }
          // the read was good: log the orec
          tx->r_orecs.insert(o);
          // do we need to extend our timestamp?
          if (rver1 > tx->start_time) {
              uintptr_t newts = timestamp.val;
              CFENCE;
              SwissValidateInflight(tx);
              CFENCE;
              tx->start_time = newts;
          }
          return tmp;
      }
  }

  /**
   *  SwissTM write
   */
  void SwissWrite(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // put value in redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));

      // get the orec addr
      orec_t* o = get_orec(addr);

      // if I'm already the lock holder, we're done!
      if (o->v.all == tx->my_lock.all)
          return;

      while (true) {
          // look at write lock
          id_version_t ivt;
          ivt.all = o->v.all;
          // if locked, CM will either tell us to self-abort, or to continue
          if (ivt.fields.lock) {
              if (SwissCMShouldAbort(tx, ivt.fields.id))
                  tmabort();
              // check liveness before continuing
              if (tx->alive == ABORTED)
                  tmabort();
              continue;
          }

          // if I can't lock it, start over
          if (!bcasptr(&o->v.all, ivt.all, tx->my_lock.all)) {
              // check liveness before continuing
              if (tx->alive == ABORTED)
                  tmabort();
              continue;
          }

          // log this lock acquire
          tx->nanorecs.insert(nanorec_t(o, o->p));

          // if read version too high, validate and extend ts
          if (o->p > tx->start_time) {
              uintptr_t newts = timestamp.val;
              SwissValidateInflight(tx);
              tx->start_time = newts;
          }

          // notify CM & return
          SwissCMOnWrite(tx);
          return;
      }
  }

  /**
   *  commit a read-write transaction
   *
   *  Note: we don't check if we've been remote aborted here, because there
   *  are no while/continue patterns in this code.  If someone asked us to
   *  abort, we can ignore them... either we commit and zero our state,
   *  or we abort anyway.
   */
  void SwissCommit(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // read-only case
      if (!tx->writes.size()) {
          tx->r_orecs.reset();
          OnROCommit(tx);
          return;
      }

      // writing case:

      // first, grab all read locks covering the write set
      foreach (NanorecList, i, tx->nanorecs) {
          i->o->p = UINT_MAX;
      }

      // increment the global timestamp, and maybe validate
      tx->end_time = 1 + faiptr(&timestamp.val);
      if (tx->end_time > (tx->start_time + 1))
          SwissValidateCommit(tx);

      // run the redo log
      tx->writes.writeback();

      // now release all read and write locks covering the writeset
      foreach (NanorecList, i, tx->nanorecs) {
          i->o->p = tx->end_time;
          CFENCE;
          i->o->v.all = tx->end_time;
      }

      // clean up
      tx->writes.reset();
      tx->r_orecs.reset();
      tx->nanorecs.reset();
      OnRWCommit(tx);
  }

  // rollback a transaction
  void
  SwissRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking
      // the branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // now release all read and write locks covering the writeset... often,
      // we didn't acquire the read locks, but it's harmless to do it like
      // this
      if (tx->nanorecs.size()) {
          foreach (NanorecList, i, tx->nanorecs) {
              i->o->v.all = i->v;
          }
      }

      // reset lists
      tx->writes.reset();
      tx->r_orecs.reset();
      tx->nanorecs.reset();

      // contention management on rollback
      SwissCMOnRollback(tx);
      PostRollback(tx);
  }

  // Validate a transaction's read set
  //
  // for in-flight transactions, write locks don't provide a fallback when
  // read-lock validation fails
  void SwissValidateInflight(TxThread* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          if ((*i)->p > tx->start_time)
              tmabort();
      }
  }

  // validate a transaction's write set
  //
  // for committing transactions, there is a backup plan wh read-lock
  // validation fails
  void SwissValidateCommit(TxThread* tx)
  {
      foreach (OrecList, i, tx->r_orecs) {
          if ((*i)->p > tx->start_time) {
              if ((*i)->v.all != tx->my_lock.all) {
                  foreach (NanorecList, i, tx->nanorecs) {
                      i->o->p = i->v;
                  }
                  tmabort();
              }
          }
      }
  }

  // cotention managers
  void SwissCMStart(TxThread* tx)
  {
      if (!tx->consec_aborts)
          tx->cm_ts = UINT_MAX;
  }

  void SwissCMOnWrite(TxThread* tx)
  {
      if ((tx->cm_ts == UINT_MAX) && (tx->writes.size() == SWISS_PHASE2))
          tx->cm_ts = 1 + faiptr(&greedy_ts.val);
  }

  bool SwissCMShouldAbort(TxThread* tx, uintptr_t owner_id)
  {
      // if caller has MAX priority, it should self-abort
      if (tx->cm_ts == UINT_MAX)
          return true;

      // self-abort if owner's priority lower than mine
      TxThread* owner = threads[owner_id - 1];
      if (owner->cm_ts < tx->cm_ts)
          return true;

      // request owner to remote abort
      owner->alive = ABORTED;
      return false;
  }

  void SwissCMOnRollback(TxThread* tx)
  {
      exp_backoff(tx);
  }

  /*** Become irrevocable via abort-and-restart */
  bool SwissIrrevoc(TxThread*) { return false; }

  /***  Keep SwissTM metadata healthy */
  void SwissOnSwitchTo()
  {
      timestamp.val = MAXIMUM(timestamp.val, timestamp_max.val);
  }
}

REGISTER_REGULAR_ALG(Swiss, "Swiss", false)

#ifdef STM_ONESHOT_ALG_Swiss
DECLARE_AS_ONESHOT(Swiss)
#endif
