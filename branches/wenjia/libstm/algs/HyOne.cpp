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

      // the variable nesting_depth indicates the depth of current transaction.
      // if the current transaction is running inside another outside transaction, just return so that we don't have to do the actual commit.
      
      if (--tx->nesting_depth) return;
      // [cw]
      // new algorithm:
      // if (flag is true)
      //   release the lock and set flag to false
      // the variable irrevoc represents the modes this PhaseTM-serial transaction is currently working. 
      // irrevoc == 1 means that it now runs in language serial mode.
      // irrevoc == 0 means that it now runs in hardware mode.

      if(tx->irrevoc)
      {
      	//tatas_release(&timestamp.val);
	tx->irrevoc = false;
	timestamp.val = 0;
	tx->hyOne_abort_count = 0;
      }

      // otherwise
      else
      {
        //execute XEND
	asm volatile (".byte 0x0f \n\t .byte 0x01 \n\t .byte 0xd5");

      }
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

  void HyOneBegin(TX_LONE_PARAMETER);

	// Note that XBEGIN requires an abort handler.  Ours should set the
	// flag to true and then re-call HyOneBegin 
	void HyOneAbort(TX_LONE_PARAMETER)
	{
	  TX_GET_TX_INTERNAL;
	  tx->hyOne_abort_count ++;
	  while (timestamp.val == 1) {}

	  return HyOneBegin(TX_LONE_PARAMETER);
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
      tx->allocator.onTxBegin();
	//asm volatile();
	      // else
      		//   issue an XBEGIN instruction
      		//   then spin until the lock is unheld

	if (tx->nesting_depth++)
	//we are already in a transaction context
	//therefore, we do nothing, just return to a outside transaction
		return;
	//xbegin __abort_handler
	asm volatile (".byte 0xc7 \n\t .byte 0xf8 \n\t __abort_handler"
		      "__abort_handler: nop;"
				       "call HyOneAbort;");
      
      // we use the global timestamp as a lock for the Serial PhaseTM
      // If this lock is occupied by another transaction, it means that 
      // another transaction is using the resource exclusively in the 
      // language sseiral mode, so we have to abort.
	      if (timestamp.val == 1)
	      {
		  //XABORT
		  asm volatile (".byte 0xc6 \n\t .byte 0xf8 \n\t .byte 0x12");
	      }

      // if we have aborted for more than 8 times, then we grab the lock,
      // ending the hardware transaction mode and entering the language 
      // serial mode. After this, all other hardware transactions are forced to
      // abort. Note that either all hardware transactions run concurrently, 
      // or one hardware transaction is running with the lock owned and all
      // others have to wait until the lock is freed before starting another
      // hardware transaction

	      if (tx->hyOne_abort_count > 8)
	      {
		timestamp.val = 1;
		//XEND
		asm volatile (".byte 0x0f \n\t .byte 0x01 \n\t .byte 0xd5");
		tx->irrevoc = 1;
	      }

   }	

}


REGISTER_REGULAR_ALG(HyOne, "HyOne", true)

#ifdef STM_ONESHOT_ALG_HyOne
DECLARE_AS_ONESHOT(HyOne)
#endif
