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
 *  MCS Implementation
 *
 *    This STM is like CGL, except we use a single MCS lock instead of a TATAS
 *    lock.  There is no parallelism, but it is very fair.
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  TM_FASTCALL void* MCSRead(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void MCSWrite(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void MCSCommit(TX_LONE_PARAMETER);

  /**
   *  MCS begin:
   */
  void MCSBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // acquire the MCS lock
      tx->begin_wait = mcs_acquire(&mcslock, tx->my_mcslock);
      tx->allocator.onTxBegin();
  }

  /**
   *  MCS commit
   */
  void
  MCSCommit(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // release the lock, finalize mm ops, and log the commit
      mcs_release(&mcslock, tx->my_mcslock);
      OnCGLCommit(tx);
  }

  /**
   *  MCS read
   */
  void*
  MCSRead(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  MCS write
   */
  void
  MCSWrite(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      STM_DO_MASKED_WRITE(addr, val, mask);
  }

  /**
   *  MCS unwinder:
   *
   *    In MCS, aborts are never valid
   */
  void
  MCSRollback(STM_ROLLBACK_SIG(,,))
  {
      UNRECOVERABLE("ATTEMPTING TO ABORT AN IRREVOCABLE MCS TRANSACTION");
  }

  /**
   *  MCS in-flight irrevocability:
   *
   *    Since we're already irrevocable, this code should never get called.
   *    Instead, the become_irrevoc(TX_LONE_PARAMETER) call should just return true
   */
  bool
  MCSIrrevoc(TxThread*)
  {
      UNRECOVERABLE("MCSIRREVOC SHOULD NEVER BE CALLED");
      return false;
  }

  /**
   *  Switch to MCS:
   *
   *    Since no other algs use the mcslock variable, no work is needed in this
   *    function
   */
  void
  MCSOnSwitchTo() {
  }

  /**
   *  MCS initialization
   */
  template<>
  void initTM<MCS>()
  {
      // set the name
      stms[MCS].name      = "MCS";

      // set the pointers
      stms[MCS].begin     = MCSBegin;
      stms[MCS].commit    = MCSCommit;
      stms[MCS].read      = MCSRead;
      stms[MCS].write     = MCSWrite;
      stms[MCS].rollback  = MCSRollback;
      stms[MCS].irrevoc   = MCSIrrevoc;
      stms[MCS].switcher  = MCSOnSwitchTo;
      stms[MCS].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_MCS
DECLARE_AS_ONESHOT_SIMPLE(MCS)
#endif
