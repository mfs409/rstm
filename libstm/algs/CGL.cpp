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
 *  CGL Implementation
 *
 *    This is the classic STM baseline: there is no instrumentation, as all
 *    transactions are protected by the same single test-and-test-and-set lock.
 *
 *    NB: retry and restart are not supported, and we never know if a
 *        transaction is read-only or not
 */

#include "../algs.hpp"
#include "../UndoLog.hpp" // STM_DO_MASKED_WRITE
#include "../Diagnostics.hpp"

using stm::TxThread;
using stm::timestamp;
using stm::timestamp_max;


/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace
{
  struct CGL
  {
      // begin_CGL is external
      static TM_FASTCALL void* read(TX_FIRST_PARAMETER STM_READ_SIG(,));
      static TM_FASTCALL void write(TX_FIRST_PARAMETER STM_WRITE_SIG(,,  ));
      static TM_FASTCALL void commit(TX_LONE_PARAMETER);

      static void rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
  };

  /**
   *  CGL commit
   */
  void CGL::commit(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;

      // release the lock, finalize mm ops, and log the commit
      tatas_release(&timestamp.val);
      OnCGLCommit(tx);
  }

  /**
   *  CGL read
   */
  void* CGL::read(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  CGL write
   */
  void CGL::write(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      STM_DO_MASKED_WRITE(addr, val, mask);
  }

  /**
   *  CGL unwinder:
   *
   *    In CGL, aborts are never valid
   */
  void CGL::rollback(STM_ROLLBACK_SIG(,,))
  {
      stm::UNRECOVERABLE("ATTEMPTING TO ABORT AN IRREVOCABLE CGL TRANSACTION");
  }

  /**
   *  CGL in-flight irrevocability:
   *
   *    Since we're already irrevocable, this code should never get called.
   *    Instead, the become_irrevoc() call should just return true.
   */
  bool CGL::irrevoc(TxThread*)
  {
      stm::UNRECOVERABLE("CGL::IRREVOC SHOULD NEVER BE CALLED");
      return false;
  }

  /**
   *  Switch to CGL:
   *
   *    We need a zero timestamp, so we need to save its max value to support
   *    algorithms that do not expect the timestamp to ever decrease
   */
  void CGL::onSwitchTo()
  {
      timestamp_max.val = MAXIMUM(timestamp.val, timestamp_max.val);
      timestamp.val = 0;
  }
}

namespace stm
{
  /**
   *  CGL begin:
   *
   *    We grab the lock, but we count how long we had to spin, so that we can
   *    possibly adapt after releasing the lock.
   *
   *    This is external and declared in algs.hpp so that we can access it as a
   *    default in places.
   */
  void begin_CGL(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // get the lock and notify the allocator
      tx->begin_wait = tatas_acquire(&timestamp.val);
      tx->allocator.onTxBegin();
  }

  /**
   *  CGL initialization
   */
  template<>
  void initTM<CGL>()
  {
      // set the name
      stms[CGL].name      = "CGL";

      // set the pointers
      //
      // NB: thereTM is a gross hack here.  Since the CGL <class> is not
      //     visible, we cannot set the initial value of the tmbegin pointer to
      //     CGL::begin.  However, we need the initial value to be *something*,
      //     and our choice was to use a function called 'begin_CGL'.  For now,
      //     to prevent deadlocks at startup, CGL will use begin_CGL instead of
      //     CGL::begin.  In the long term, hopefully we can do better.
      stms[CGL].begin     = stm::begin_CGL;
      stms[CGL].commit    = ::CGL::commit;
      stms[CGL].read      = ::CGL::read;
      stms[CGL].write     = ::CGL::write;
      stms[CGL].rollback  = ::CGL::rollback;
      stms[CGL].irrevoc   = ::CGL::irrevoc;
      stms[CGL].switcher  = ::CGL::onSwitchTo;
      stms[CGL].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_CGL
DECLARE_AS_ONESHOT_SIMPLE(CGL)
#endif
