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
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */

volatile int lock = 0;

namespace stm
{
  /**
   *  HyOne commit
   */
  TM_FASTCALL
  void HyOneCommit(TX_LONE_PARAMETER)
  {
//      TX_GET_TX_INTERNAL;

      // [mfs] Why are we asserting this?
//      assert(tx->nesting_depth == 0);

      // [mfs] Are these comments still needed?  If so, they should probably
      //       be moved up to the method's comment, so that the algorithm is
      //       cleanly documented in one place, rather than inline.
      
      // the variable irrevoc represents the modes this PhaseTM-serial transaction is currently working.
      // irrevoc == 1 means that it now runs in language serial mode.
      // irrevoc == 0 means that it now runs in hardware mode.

      // [mfs] the comment here should explain what is going on.  E.g., "If
      // tx->irrevoc is true, then this "transaction" is not actually using
      // the hardware TM right now, and it holds a lock.  We can just release
      // the lock to commit."
/*
      if (tx->irrevoc) {
          tx->irrevoc = false;
          timestamp.val = 0;
          tx->hyOne_abort_count = 0;
      }
      else {
          _xend();
      }
*/
      
	lock = 0;
	// [mfs] I am worried about us not handling mm correctly.  Why aren't
      	// we calling OnCGLCommit(tx) here?

      // [chw] I am still unclear why we have to call this, but not some other functions like OnRWCommit(tx);
      // BTW, isn't CGL another algorithm? Why do we have to call a function related to **another** algorithm?
      // finalize mm ops, and log the commit
//      OnCGLCommit(tx);
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

  void HyOneRollback(STM_ROLLBACK_SIG(,,))
  {
//      UNRECOVERABLE("ATTEMPTING TO ABORT AN IRREVOCABLE HyOne TRANSACTION");
  }

  /**
   *  HyOne in-flight irrevocability:
   *
   *    Since we're already irrevocable, this code should never get called.
   *    Instead, the become_irrevoc() call should just return true.
   *
   *    [mfs] Is it that we just don't support irrevocability in the
   *          traditional sense yet?
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

  void HyOneAbort(TX_LONE_PARAMETER)
  {
      // Note that XBEGIN requires an abort handler.  Ours should set the
      // flag to true and then re-call HyOneBegin
      TX_GET_TX_INTERNAL;
      tx->hyOne_abort_count++;

//      while (timestamp.val == 1) {}

      // [mfs] I don't quite follow the control flow here.  There is no call
      //       to tmabort(), so how is the stack getting rolled back?  How
      //       are we returning to HyOneBegin()?  Note that we are still
      //       using RSTM under the hood, which means that we *have* a full
      //       setjmp checkpoint at our disposal and available for restarting
      //       things, and since we are in an abort situation, a setjmp is
      //       effectively "backoff"...

      tmabort();
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
//	TX_GET_TX_INTERNAL;

      	// [mfs] Do we need this assert?
//      	assert (tx->nesting_depth == 1);
      
	//we are already in a transaction context
	//therefore, we do nothing, just return to a outside transaction
top:
	unsigned int status = _xbegin();
	if (status == _XBEGIN_STARTED) {

		if (lock == 0)
	  	{	
			lock = 1;
			_xend();
		}
	  	else
			_xabort();
          // we use the global timestamp as a lock for the Serial PhaseTM
          // If this lock is occupied by another transaction, it means that
          // another transaction is using the resource exclusively in the
          // language sseiral mode, so we have to abort


          // [mfs] Comment here should indicate what is going to happen:
          // i.e., calling _xabort() will cause the hardware transaction to
          // abort, which sends us back to the _xbegin() function, but the
          // return value now will be XXX, which will cause us to call some
          // special handler...
//	 _xabort();
/*   
         if (timestamp.val == 1) {
              _xabort();
          }
*/
          // if we have aborted for more than 8 times, then we grab the lock,
          // ending the hardware transaction mode and entering the language
          // serial mode. After this, all other hardware transactions are
          // forced to abort. Note that either all hardware transactions run
          // concurrently, or one hardware transaction is running with the
          // lock owned and all others have to wait until the lock is
          // released before starting another hardware transaction
/*
          if (tx->hyOne_abort_count > 8) {
              timestamp.val = 1;
              _xend();
              tx->irrevoc = 1;
          }

*/
//	  tx->allocator.onTxBegin();
      }
      // transaction fails to start, or abort. this is the fallback execution path
      else {
	  printf("aborting...status = %d", status);
	  goto top;
//          HyOneAbort(TX_LONE_PARAMETER);
      }
  }
}

REGISTER_REGULAR_ALG(HyOne, "HyOne", true)

#ifdef STM_ONESHOT_ALG_HyOne
DECLARE_AS_ONESHOT(HyOne)
#endif
