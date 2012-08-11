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
 *  This file implements the code for installing an algorithm.
 *
 *  [mfs] We should actually back the function pointers here
 */

#include <setjmp.h>
#include <sys/mman.h>
#include "inst.hpp"
#include "policies.hpp"
#include "algs.hpp"

namespace stm
{
#ifndef STM_ONESHOT_MODE
  void install_algorithm_local(int new_alg)
  {
      // set my read/write/commit pointers
      tmread     = stms[new_alg].read;
      tmwrite    = stms[new_alg].write;
      tmcommit   = stms[new_alg].commit;
  }
#else
  void install_algorithm_local(int) { }
#endif

  /**
   *  Switch all threads to use a new STM algorithm.
   *
   *  Logically, there is an invariant that nobody is in a transaction.  This
   *  is not easy to define, though, because a thread may call this with a
   *  non-null scope, which is our "in transaction" flag.  In practice, such
   *  a thread is calling install_algorithm from the end of either its abort
   *  or commit code, so it is 'not in a transaction'
   *
   *  Another, and more important invariant, is that the caller must have
   *  personally installed begin_blocker.  There are three reasons to install
   *  begin_blocker: irrevocability, thread creation, and mode switching.
   *  Each of those actions, independently, can only be done by one thread at
   *  a time.  Furthermore, no two of those actions can be done
   *  simultaneously.
   */
  void install_algorithm(int new_alg, TxThread* tx)
  {
      // [mfs] when is tx null?
      // diagnostic message
      if (tx)
          printf("[%u] switching from %s to %s\n", tx->id,
                 stms[curr_policy.ALG_ID].name, stms[new_alg].name);
      if (!stms[new_alg].privatization_safe)
          printf("Warning: Algorithm %s is not privatization-safe!\n",
                 stms[new_alg].name);

      // we need to make sure the metadata remains healthy
      //
      // we do this by invoking the new alg's onSwitchTo_ method, which
      // is responsible for ensuring the invariants that are required of shared
      // and per-thread metadata while the alg is in use.
      stms[new_alg].switcher();
      CFENCE;

      // set per-thread pointers
      for (unsigned i = 0; i < threadcount.val; ++i) {
#ifndef STM_ONESHOT_MODE
          *(threads[i]->my_tmread)     = (void*)stms[new_alg].read;
          *(threads[i]->my_tmwrite)    = (void*)stms[new_alg].write;
          *(threads[i]->my_tmcommit)   = (void*)stms[new_alg].commit;
#endif
          threads[i]->consec_aborts  = 0;
      }

#ifndef STM_ONESHOT_MODE
      tmirrevoc  = stms[new_alg].irrevoc;
      tmrollback = stms[new_alg].rollback;
#endif
      curr_policy.ALG_ID   = new_alg;
      CFENCE;
      tmbegin    = stms[new_alg].begin;
  }

#ifndef STM_CHECKPOINT_ASM
  /**
   *  The default mechanism that libstm uses for an abort. An API environment
   *  may also provide its own abort mechanism (see itm2stm for an example of
   *  how the itm shim does this).
   *
   *  This is ugly because rollback has a configuration-dependent signature.
   */
  NORETURN void tmabort()
  {
      stm::TxThread* tx = stm::Self;
#if defined(STM_ABORT_ON_THROW)
      tmrollback(tx, NULL, 0);
#else
      tmrollback(tx);
#endif
      jmp_buf* scope = (jmp_buf*)tx->checkpoint;
      longjmp(*scope, 1);
  }
#else
  NORETURN
  void tmabort()
  {
      stm::TxThread* tx = stm::Self;
#if defined(STM_ABORT_ON_THROW)
      tmrollback(tx, NULL, 0);
#else
      tmrollback(tx);
#endif
      tx->nesting_depth = 1;          // no closed nesting yet.
      restore_checkpoint(stm::tmbegin);
  }
#endif

#ifndef STM_ONESHOT_MODEQQQ // [mfs] Need to take this out, but it's going to
                            // hurt...
  /**
   *  The begin function pointer.  Note that we need tmbegin to equal
   *  begin_cgl initially, since "0" is the default algorithm
   */
  void (*volatile tmbegin)(TX_LONE_PARAMETER) = begin_CGL;
#endif

  /**
   *  The tmrollback and tmirrevoc pointers
   */

#ifndef STM_ONESHOT_MODE
  void (*tmrollback)(STM_ROLLBACK_SIG(,,));
  bool (*tmirrevoc)(TxThread*) = NULL;
#endif

  /**
   *  Simplified support for self-abort
   *
   *  [mfs] This is not in the API, and cancel *is*, so should we drop this?
   *        Note that STAMP still needs it until Labyrinth is fixed...
   */
  void restart()
  {
      // get the thread's tx context
      TxThread* tx = Self;
      // register this restart
      ++tx->num_restarts;
      // call the abort code
      stm::tmabort();
  }

#ifndef STM_ONESHOT_MODE
  /**
   * The function pointers:
   */
  THREAD_LOCAL_DECL_TYPE(TM_FASTCALL void(*tmcommit)(TX_LONE_PARAMETER));
  THREAD_LOCAL_DECL_TYPE(TM_FASTCALL void*(*tmread)(TX_FIRST_PARAMETER STM_READ_SIG(,)));
  THREAD_LOCAL_DECL_TYPE(TM_FASTCALL void(*tmwrite)(TX_FIRST_PARAMETER STM_WRITE_SIG(,,)));
#endif

#ifndef STM_CHECKPOINT_ASM
  /**
   *  Code to start a transaction.  We assume the caller already performed a
   *  setjmp, and is passing a valid setjmp buffer to this function.
   *
   *  The code to begin a transaction *could* all live on the far side of a
   *  function pointer.  By putting some of the code into this inlined
   *  function, we can:
   *
   *    (a) avoid overhead under subsumption nesting and
   *    (b) avoid code duplication or MACRO nastiness
   */
  void begin(TX_FIRST_PARAMETER scope_t* s, uint32_t /*abort_flags*/)
  {
      TX_GET_TX_INTERNAL;
      if (++tx->nesting_depth > 1)
          return;

// if we're in oneshot mode, we don't ever change algs, so we don't need a
// WBR on TM begin.  While it's tempting to try to tie this to ProfileTM
// (e.g., !STM_PROFILETMTRIGGER_NONE), that's insufficient since we have TOL
// and NOL algorithms that switch between TML/Nano and OrecLazy, without
// using Profiles.  They could break without the WBR.
#ifdef STM_ONESHOT_MODE
      tx->checkpoint = s;
      tx->in_tx = 1;
#else
      // we must ensure that the write of the transaction's scope occurs
      // *before* the read of the begin function pointer.  On modern x86, a
      // CAS is faster than using WBR or xchg to achieve the ordering.  On
      // SPARC, WBR is best.
      tx->checkpoint = s;
#ifdef STM_CPU_SPARC
      tx->in_tx = 1;
      WBR;
#else
      // [mfs] faiptr might be better...
      (void)casptr(&tx->in_tx, 0, 1);
#endif
#endif

#ifndef STM_PROFILETMTRIGGER_NONE
      // some adaptivity mechanisms need to know nontransactional and
      // transactional time.  This code suffices, because it gets the time
      // between transactions.  If we need the time for a single transaction,
      // we can run ProfileTM
      if (tx->end_txn_time)
          tx->total_nontxn_time += (tick() - tx->end_txn_time);
#endif
      // now call the per-algorithm begin function
      tmbegin(TX_LONE_ARG);
  }
#endif

  /**
   *  Code to commit a transaction.  As in begin(), we are using forced
   *  inlining to save a little bit of overhead for subsumption nesting, and to
   *  prevent code duplication.
   */
  void commit(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // don't commit anything if we're nested... just exit this scope
      if (--tx->nesting_depth)
          return;

      // dispatch to the appropriate end function
      tmcommit(TX_LONE_ARG);

      // indicate "not in tx"
      CFENCE;
      tx->in_tx = 0;
#ifndef STM_PROFILETMTRIGGER_NONE
      // record start of nontransactional time
      tx->end_txn_time = tick();
#endif
  }

} // namespace stm
