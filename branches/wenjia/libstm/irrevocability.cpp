/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "../include/macros.hpp"  // barrier signatures
#include "txthread.hpp"           // TxThread stuff
#include "policies.hpp"           // curr_policy
#include "Diagnostics.hpp"
#include "inst.hpp"
#include "algs/tml_inline.hpp"

using stm::TxThread;
using stm::stms;
using stm::curr_policy;
using stm::CGL;

namespace
{
  /**
   *  Handler for rollback attempts while irrevocable. Useful for trapping
   *  problems early.
   */
  void rollback_irrevocable(STM_ROLLBACK_SIG(,,))
  {
      stm::UNRECOVERABLE("Irrevocable thread attempted to rollback.");
  }

  /**
   *  Resets all of the barriers to be the curr_policy bariers, except for
   *  tmabort which reverts to the one we saved, and tmbegin which should be
   *  done manually in the caller.
   *
   *  [mfs] We should call out to inst.cpp/hpp here...
   */
  inline void unset_irrevocable_barriers()
  {
#ifndef STM_ONESHOT_MODE
      stm::tmread     = stms[curr_policy.ALG_ID].read;
      stm::tmwrite    = stms[curr_policy.ALG_ID].write;
      stm::tmcommit   = stms[curr_policy.ALG_ID].commit;
      stm::tmirrevoc  = stms[curr_policy.ALG_ID].irrevoc;
      stm::tmrollback = stms[curr_policy.ALG_ID].rollback;
#else
      stm::UNRECOVERABLE("Irrevocability does not work with ONESHOT mode");
#endif
  }

  /**
   *  custom commit for irrevocable transactions
   */
  TM_FASTCALL void commit_irrevocable(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;

      // make self non-irrevocable, and unset local r/w/c barriers
      tx->irrevocable = false;
      unset_irrevocable_barriers();
      // now allow other transactions to run
      CFENCE;
      stm::tmbegin = stms[curr_policy.ALG_ID].begin;
      // finally, call the standard commit cleanup routine
      OnROCommit(tx);
  }

  /**
   *  Sets all of the barriers to be irrevocable, except tmbegin.
   *
   *  [mfs] We should call inst.cpp/hpp here...
   */
  inline void set_irrevocable_barriers()
  {
#ifndef STM_ONESHOT_MODE
      stm::tmread         = stms[CGL].read;
      stm::tmwrite        = stms[CGL].write;
      stm::tmcommit       = commit_irrevocable;
      stm::tmirrevoc = stms[CGL].irrevoc;
      stm::tmrollback       = rollback_irrevocable;
#else
      stm::UNRECOVERABLE("Irrevocability does not work with ONESHOT mode");
#endif
  }
}

namespace stm
{
  /**
   *  The 'Serial' algorithm requires a custom override for irrevocability,
   *  which we implement here.
   */
  void serial_irrevoc_override(TxThread* tx);

  /**
   *  Try to become irrevocable, inflight. This happens via mode
   *  switching. If the inflight irrevocability fails, we fall-back to an
   *  abort-and-restart-as-irrevocable scheme, based on the understanding
   *  that the begin_blocker tmbegin barrier will configure us as irrevocable
   *  and let us through if we have our irrevocable flag set. In addition to
   *  letting us through, it will set our barrier pointers to be the
   *  irrevocable barriers---it has to be done here because the rollback that
   *  the abort triggers will reset anything we try and set here.
   */
  void become_irrevoc()
  {
      TxThread* tx = Self;
      // special code for degenerate STM implementations
      //
      // NB: stm::is_irrevoc relies on how this works, so if it changes then
      //     please update that code as well.
#ifndef STM_ONESHOT_MODE
      if (tmirrevoc == stms[CGL].irrevoc)
          return;
#endif
      if ((curr_policy.ALG_ID == MCS) || (curr_policy.ALG_ID == Ticket))
          return;

      if (curr_policy.ALG_ID == Serial) {
          serial_irrevoc_override(tx);
          return;
      }

      if (curr_policy.ALG_ID == TML) {
          if (!tx->tmlHasLock)
              beforewrite_TML(tx);
          return;
      }

      // prevent new txns from starting.  If this fails, it means one of
      // three things:
      //
      //  - Someone else became irrevoc
      //  - Thread creation is in progress
      //  - Adaptivity is in progress
      //
      //  The first of these cases requires us to abort, because the irrevoc
      //  thread is running the 'wait for everyone' code that immediately
      //  follows this CAS.  Since we can't distinguish the three cases,
      //  we'll just abort all the time.  The impact should be minimal.
      if (!bcasptr(&stm::tmbegin, stms[curr_policy.ALG_ID].begin,
                   &begin_blocker))
          tmabort();

      // wait for everyone to be out of a transaction (scope == NULL)
      for (unsigned i = 0; i < threadcount.val; ++i)
          while ((i != (tx->id-1)) && (threads[i]->in_tx))
              spin64();

      // try to become irrevocable inflight
      tx->irrevocable = tmirrevoc(tx);

      // If inflight succeeded, switch our barriers and return true.
      if (tx->irrevocable) {
          set_irrevocable_barriers();
          return;
      }

      // Otherwise we tmabort (but mark ourselves as irrevocable so that we get
      // through the begin_blocker after the abort). We don't switch the barriers
      // here because a) one of the barriers that we'd like to switch is
      // rollback, which is used by tmabort and b) rollback is designed to reset
      // our barriers to the default read-only barriers for the algorithm which
      // will just overwrite what we do here
      //
      // begin_blocker sets our barriers to be irrevocable if we have our
      // irrevocable flag set.
      tx->irrevocable = true;
      stm::tmabort();
  }

  /**
   * True if the current algorithm is irrevocable.
   */
  bool is_irrevoc(const TxThread& tx)
  {
#ifndef STM_ONESHOT_MODE
      if (tx.irrevocable || tmirrevoc == stms[CGL].irrevoc)
          return true;
#else
      if (tx.irrevocable)
          return true;
#endif
      if ((curr_policy.ALG_ID == MCS) || (curr_policy.ALG_ID  == Ticket))
          return true;
      if ((curr_policy.ALG_ID == TML) && (tx.tmlHasLock))
          return true;
      if (curr_policy.ALG_ID == Serial)
          return true;
      return false;
  }

  /**
   *  Custom begin method that blocks the starting thread, in order to get
   *  rendezvous correct during mode switching and GRL irrevocability. It
   *  doubles as an irrevocability mechanism for implementations where we don't
   *  have (or can't write) an in-flight irrevocability mechanism.
   */
  void begin_blocker(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;

      // if the caller is trying to restart as irrevocable, let them
      if (tx->irrevocable) {
          set_irrevocable_barriers();
          return;
      }

      // adapt without longjmp
      void (*beginner)(TX_LONE_PARAMETER);
      while (true) {
          // first, clear the outer scope, because it's our 'tx/nontx' flag
          tx->in_tx = 0;
          // next, wait for the begin_blocker to be uninstalled
          while (tmbegin == begin_blocker)
              spin64();
          CFENCE;
          // now re-state that we are in tx
          tx->in_tx = 1; WBR;

          // read the begin function pointer AFTER setting the scope
          beginner = tmbegin;
          // if begin_blocker is no longer installed, we can call the pointer
          // to start a transaction, and then return.  Otherwise, we missed our
          // window, so we need to go back to the top of the loop.
          if (beginner != begin_blocker)
              break;
      }
      beginner(TX_LONE_ARG);
  }
}

