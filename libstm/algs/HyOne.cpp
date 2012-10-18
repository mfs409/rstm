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
 *  HyOne Implementation
 *
 *    This is the classic STM baseline: there is no instrumentation, as all
 *    transactions are protected by the same single test-and-test-and-set lock.
 *
 *    NB: retry and restart are not supported, and we never know if a
 *        transaction is read-only or not
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace stm
{
  /**
   *  HyOne commit
   */
  TM_FASTCALL
  void HyOneCommit(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;

      // [cw]
      // new algorithm:
      // if (flag is true)
      //   release the lock and set flag to false
      tatas_release(&timestamp.val);


      // otherwise
      // execute XEND

      // finalize mm ops, and log the commit
      OnCGLCommit(tx);
  }

  /**
   *  HyOne read
   */
  TM_FASTCALL
  void* HyOneRead(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  HyOne write
   */
  TM_FASTCALL
  void HyOneWrite(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      STM_DO_MASKED_WRITE(addr, val, mask);
  }

  /**
   *  HyOne unwinder:
   *
   *    In HyOne, aborts are never valid
   */
  void HyOneRollback(STM_ROLLBACK_SIG(,,))
  {
      UNRECOVERABLE("ATTEMPTING TO ABORT AN IRREVOCABLE HyOne TRANSACTION");
  }

  /**
   *  HyOne in-flight irrevocability:
   *
   *    Since we're already irrevocable, this code should never get called.
   *    Instead, the become_irrevoc() call should just return true.
   */
  bool HyOneIrrevoc(TxThread*)
  {
      UNRECOVERABLE("HyOneIRREVOC SHOULD NEVER BE CALLED");
      return false;
  }

  /**
   *  Switch to HyOne:
   *
   *    We need a zero timestamp, so we need to save its max value to support
   *    algorithms that do not expect the timestamp to ever decrease
   */
  void HyOneOnSwitchTo()
  {
      timestamp_max.val = MAXIMUM(timestamp.val, timestamp_max.val);
      timestamp.val = 0;
  }

  /**
   *  HyOne begin:
   *
   *    We grab the lock, but we count how long we had to spin, so that we can
   *    possibly adapt after releasing the lock.
   *
   *    This is external and declared in algs.hpp so that we can access it as a
   *    default in places.
   */
  void HyOneBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;

      // [cw] we will need to add a flag to the TxThread object in the
      // txthread.hpp file.  The flag will be a bool that is true if we are
      // supposed to start in software mode, and false otherwise

      // [cw]
      // algorithm here is:
      //
      // if (flag == true) then acquire the lcok

      // get the lock and notify the allocator
      tx->begin_wait = tatas_acquire(&timestamp.val);

      // else
      //   issue an XBEGIN instruction
      //   then spin until the lock is unheld

      // Note that XBEGIN requires an abort handler.  Ours should set the
      // flag to true and then re-call HyOneBegin

      tx->allocator.onTxBegin();
  }
}

REGISTER_REGULAR_ALG(HyOne, "HyOne", true)

#ifdef STM_ONESHOT_ALG_HyOne
DECLARE_AS_ONESHOT(HyOne)
#endif
