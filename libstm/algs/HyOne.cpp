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

unsigned int _xbegin(void);
void _xend(void);
void _xabort(void);

#define _XBEGIN_STARTED  (~0u)
#define _XABORT_EXPLICIT (1 << 0)
#define _XABORT_RETRY    (1 << 1)
#define _XABORT_CONFLICT (1 << 2)
#define _XABORT_CAPACITY (1 << 3)
#define _XABORT_DEBUG    (1 << 4)
#define _XABORT_NESTED   (1 << 5)

// [mfs] This looks like it was taken directly from GCC... citation needed?
//  
// starts an RTM code region and returns a value indicating whether the
// transaction successfully started or, in the case of an abort, the abort
// code
//
// If the logical processor was not already in transactional execution, then
// the xbegin instruction causes the logical processor to start transactional
// execution.  The xbegin instruction that transitions the logical processor
// into transactional execution is referred to as the outermost xbegin
// instruction.
//
// The xbegin instruction specifies a relative offset to the fallback code
// path executed following a transactional abort. To promote proper program
// structure, this is not exposed in C++ code and the intrinsic function
// operates as if it invoked the following model code

// [mfs] We should rename this to TSX_BEGIN, then employ an ifdef that
//       defines HTM_BEGIN as TSX_BEGIN
#define TSX_BEGIN				\
    ".byte 0xc7 \n\t"                           \
    ".byte 0xf8 \n\t"                           \
    ".byte 0x00 \n\t"				\
    ".byte 0x00 \n\t"				\
    ".byte 0x00 \n\t"				\
    ".byte 0x00 \n\t"

#define TSX_END					\
    ".byte 0x0f \n\t"				\
    ".byte 0x01 \n\t"				\
    ".byte 0xd5 \n\t"

#define TSX_ABORT				\
    ".byte 0xc6 \n\t"				\
    ".byte 0xf8 \n\t"				\
    ".byte 0x12 \n\t"

/**
 *  [mfs] Should have a description here of whatever is going on.  In this
 *        case, it looks like what we are doing is having a status variable
 *        that we initialize to -1, then set within the transaction to 0.
 *        The return value thus lets us know if we are in a TX or not.
 *
 *  [mfs] This should probably move into some platform/arch folder, since it
 *        will be common to many Hybrid TM algs.
 */
inline unsigned int _xbegin(void)
{
    unsigned status;
    // [mfs] I presume this is debug code that needs to go away?
    fprintf(stdout, "into xbegin\n");

    // [mfs] Should we be using _XBEGIN_STARTED here?
    asm volatile("movl $0xffffffff, %%eax \n\t"
                 TSX_BEGIN " \n\t"
                 "movl %%eax, %0"
                 :"=r"(status));

    // [mfs] Again, this probably needs to be deleted?  In fact, printfs from
    //       a hardware context are invalid, so this *can't* be correct!
    fprintf(stdout, "into xbegin -- end\n");
    return status;
}

// Specifies the end of restricted transactional memory code region. If this
// is the outermost transaction (including this xend instruction, the number
// of xbegin matches the number of xend instructions) then the processor will
// attempt to commit processor state automatically.
//
// [mfs] I think we make sure in our code to never use the hardware nesting
//       stuff... is that correct?
//
inline void _xend(void)
{
    asm volatile(TSX_END " \n\t");
}

// Forces an RTM region to abort. All outstanding transactions are aborted
// and the logical processor resumes execution at the fallback address
// computed through the outermost xbegin transaction.
//
// The EAX register is updated to reflect an xabort instruction caused the
// abort, and the imm8 argument will be provided in the upper eight bits of
// the return value (EAX register bits 31:24) containing the indicated
// immediate value. The argument of xabort function must be a compile time
// constant.
//
// [mfs] It seems we are hard-coding the imm8 value.  What are we encoding it
//       as?  Can we elevate the value to a constant of some sort?
inline void _xabort(void)
{
    asm volatile(TSX_ABORT " \n\t");
}

namespace stm
{
  /**
   *  HyOne commit
   */
  TM_FASTCALL
  void HyOneCommit(TX_LONE_PARAMETER)
  {
      // [mfs] debug message, should be removed...
      fprintf(stdout, "Commit!!\n");
      
      TX_GET_TX_INTERNAL;

      // [mfs] Why are we asserting this?
      assert(tx->nesting_depth == 0);

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
      if (tx->irrevoc) {
          tx->irrevoc = false;
          timestamp.val = 0;
          tx->hyOne_abort_count = 0;
      }
      else {
          _xend();
      }

      // [mfs] I am worried about us not handling mm correctly.  Why aren't
      // we calling OnCGLCommit(tx) here?

      // [chw] I am still unclear why we have to call this, but not some other functions like OnRWCommit(tx);
      // BTW, isn't CGL another algorithm? Why do we have to call a function related to **another** algorithm?
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

  void HyOneRollback(STM_ROLLBACK_SIG(,,))
  {
      UNRECOVERABLE("ATTEMPTING TO ABORT AN IRREVOCABLE HyOne TRANSACTION");
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

      while (timestamp.val == 1) {}

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
	TX_GET_TX_INTERNAL;

	fprintf(stdout, "begin2\n");

      	// [mfs] Do we need this assert?
      	assert (tx->nesting_depth == 1);
      
	//we are already in a transaction context
	//therefore, we do nothing, just return to a outside transaction

	unsigned int status = _xbegin();
	if (status == _XBEGIN_STARTED) {

          // we use the global timestamp as a lock for the Serial PhaseTM
          // If this lock is occupied by another transaction, it means that
          // another transaction is using the resource exclusively in the
          // language sseiral mode, so we have to abort


          // [mfs] Comment here should indicate what is going to happen:
          // i.e., calling _xabort() will cause the hardware transaction to
          // abort, which sends us back to the _xbegin() function, but the
          // return value now will be XXX, which will cause us to call some
          // special handler...
          if (timestamp.val == 1) {
              _xabort();
          }

          // if we have aborted for more than 8 times, then we grab the lock,
          // ending the hardware transaction mode and entering the language
          // serial mode. After this, all other hardware transactions are
          // forced to abort. Note that either all hardware transactions run
          // concurrently, or one hardware transaction is running with the
          // lock owned and all others have to wait until the lock is
          // released before starting another hardware transaction
          if (tx->hyOne_abort_count > 8) {
              timestamp.val = 1;
              _xend();
              tx->irrevoc = 1;
          }

          // [mfs] unnecessary debug message
          fprintf(stdout, "begin4\n");
      }
      // transaction fails to start, or abort. this is the fallback execution path
      else {
          // [mfs] unnecessary debug message
          fprintf(stdout, "begin5\n");
          HyOneAbort(TX_LONE_PARAMETER);
      }
  }
}

REGISTER_REGULAR_ALG(HyOne, "HyOne", true)

#ifdef STM_ONESHOT_ALG_HyOne
DECLARE_AS_ONESHOT(HyOne)
#endif
