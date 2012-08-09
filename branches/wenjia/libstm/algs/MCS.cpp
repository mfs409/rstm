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

#include "../profiling.hpp"
#include "../algs.hpp"
#include "../UndoLog.hpp" // STM_DO_MASKED_WRITE

using stm::UNRECOVERABLE;
using stm::TxThread;
using stm::mcslock;


/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace  {
  struct MCS
  {
      static void begin(TX_LONE_PARAMETER);
      static TM_FASTCALL void* read(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void write(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
      static TM_FASTCALL void commit(TX_LONE_PARAMETER);

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
  };


  /**
   *  MCS begin:
   */
  void MCS::begin(TX_LONE_PARAMETER)
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
  MCS::commit(TX_LONE_PARAMETER)
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
  MCS::read(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  MCS write
   */
  void
  MCS::write(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      STM_DO_MASKED_WRITE(addr, val, mask);
  }

  /**
   *  MCS unwinder:
   *
   *    In MCS, aborts are never valid
   */
  void
  MCS::rollback(STM_ROLLBACK_SIG(,,))
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
  MCS::irrevoc(TxThread*)
  {
      UNRECOVERABLE("MCS::IRREVOC SHOULD NEVER BE CALLED");
      return false;
  }

  /**
   *  Switch to MCS:
   *
   *    Since no other algs use the mcslock variable, no work is needed in this
   *    function
   */
  void
  MCS::onSwitchTo() {
  }
}

namespace stm {
  /**
   *  MCS initialization
   */
  template<>
  void initTM<MCS>()
  {
      // set the name
      stms[MCS].name      = "MCS";

      // set the pointers
      stms[MCS].begin     = ::MCS::begin;
      stms[MCS].commit    = ::MCS::commit;
      stms[MCS].read      = ::MCS::read;
      stms[MCS].write     = ::MCS::write;
      stms[MCS].rollback  = ::MCS::rollback;
      stms[MCS].irrevoc   = ::MCS::irrevoc;
      stms[MCS].switcher  = ::MCS::onSwitchTo;
      stms[MCS].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_MCS
DECLARE_AS_ONESHOT_SIMPLE(MCS)
#endif
