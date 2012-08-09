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

      tmrollback = stms[new_alg].rollback;
      tmirrevoc  = stms[new_alg].irrevoc;
      curr_policy.ALG_ID   = new_alg;
      CFENCE;
      tmbegin    = stms[new_alg].begin;
  }

} // namespace stm
