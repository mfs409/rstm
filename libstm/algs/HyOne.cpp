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

unsigned int _xbegin(void); 
void _xend(void); 
void _xabort(void); 

#define _XBEGIN_STARTED (~0u) 
#define _XABORT_EXPLICIT (1 << 0) 
#define _XABORT_RETRY (1 << 1) 
#define _XABORT_CONFLICT (1 << 2) 
#define _XABORT_CAPACITY (1 << 3) 
#define _XABORT_DEBUG (1 << 4) 
#define _XABORT_NESTED (1 << 5)

//starts a RTM code region and returns a value indicating transaction successfully started or status from a transaction abort.

//If the logical processor was not already in transactional execution, then the xbegin instruction causes the logical processor to start transactional execution. The xbegin instruction that transitions the logical processor into transactional execution is referred to as the outermost xbegin instruction.

//The xbegin instruction specifies a relative offset to the fallback code path executed following a transactional abort. To promote proper program structure, this is not exposed in C++ code and the intrinsic function operates as if it invoked the following model code
  inline unsigned int _xbegin(void)
  {
	unsigned status;
	asm volatile ("movl $0xffffffff, %%eax;"
		      ".byte 0xc7 \n\t .byte 0xf8 \n\t __txnL1"
		      "__txnL1: movl %%eax, %0;"
			:"=r"(status));
	return status;
  }

//Specifies the end of restricted transactional memory code region. If this is the outermost transaction (including this xend instruction, the number of xbegin matches the number of xend instructions) then the processor will attempt to commit processor state automatically.
  inline void _xend(void)
  {
	asm volatile (".byte 0x0f \n\t .byte 0x01 \n\t .byte 0xd5;");
  }

//Forces a RTM region to abort. All outstanding transactions are aborted and the logical processor resumes execution at the fallback address computed through the outermost xbegin transaction.

//The EAX register is updated to reflect an xabort instruction caused the abort, and the imm8 argument will be provided in the upper eight bits of the return value (EAX register bits 31:24) containing the indicated immediate value. The argument of xabort function must be a compile time constant.
  inline void _xabort(void)
  {
	asm volatile (".byte 0xc6 \n\t .byte 0xf8 \n\t .byte 0x12");
  }

namespace stm
{
  /**
   *  HyOne commit
   */
  TM_FASTCALL
  void HyOneCommit(TX_LONE_PARAMETER)
  {

      fprintf(stdout, "Commit!!\n");
      TX_GET_TX_INTERNAL;

      // the variable nesting_depth indicates the depth of current transaction.
      // if the current transaction is running inside another outside transaction, just return so that we don't have to do the actual commit.
  	
	assert(tx->nesting_depth == 0);
//      if (--tx->nesting_depth) return;
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
	_xend();

      }
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

	void HyOneAbort(TX_LONE_PARAMETER)
	{
	
		// Note that XBEGIN requires an abort handler.  Ours should set the
		// flag to true and then re-call HyOneBegin 	
		TX_GET_TX_INTERNAL;
		tx->hyOne_abort_count ++;
//		while (timestamp.val == 1) {}

//		HyOneBegin(TX_LONE_PARAMETER);
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
		
		fprintf(stdout, "begin\n");

		// [cw] we will need to add a flag to the TxThread object in the
		// txthread.hpp file.  The flag will be a bool that is true if we are
		// supposed to start in software mode, and false otherwise

//		tx->allocator.onTxBegin();
		// [cw]
		// algorithm here is:
		//
		// if (flag == true) then acquire the lcok
		// get the lock and notify the allocator	
		//asm volatile();
		      // else
			//   issue an XBEGIN instruction
			//   then spin until the lock is unheld

		assert (tx->nesting_depth == 1);
		//we are already in a transaction context
		//therefore, we do nothing, just return to a outside transaction

		fprintf(stdout, "begin2\n");
		unsigned int status;	
		if ((status = _xbegin()) == _XBEGIN_STARTED)
		{
			// we use the global timestamp as a lock for the Serial PhaseTM
			// If this lock is occupied by another transaction, it means that 
			// another transaction is using the resource exclusively in the 
			// language sseiral mode, so we have to abort.
			
			fprintf(stdout, "begin3\n");
			if (timestamp.val == 1)
			{
				//XABORT
				_xabort();

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
				_xend();
				tx->irrevoc = 1;
			}

	
			fprintf(stdout, "begin4\n");
		}
		else  //transaction fails to start, or abort. this is the fallback execution path
		{

			fprintf(stdout, "begin5\n");
			HyOneAbort(TX_LONE_PARAMETER);		
		}
	}	

}


REGISTER_REGULAR_ALG(HyOne, "HyOne", true)

#ifdef STM_ONESHOT_ALG_HyOne
DECLARE_AS_ONESHOT(HyOne)
#endif
