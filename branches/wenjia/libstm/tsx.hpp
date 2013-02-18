/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef STM_TSX_H
#define STM_TSX_H


/* this header file contains the tsx wrapper functions for using hardware TM */

namespace stm
{
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
	// The following description of the three functions _xbegin, _xend, and _xabort // comes from Intel@C++ Compiler XE13.0 User and Reference Guides
	 
	// starts an RTM code region and returns a value indicating transaction successfully started or status from a transaction abort
	//
	// If the logical processor was not already in transactional execution, then
	// the TSX_BEGIN instruction causes the logical processor to start 
	// transactional execution.  The TSX_BEGIN instruction that transitions the logical processor into transactional execution is referred to as the outermost TSX_BEGIN instruction.
	//
	// The TSX_BEGIN instruction specifies a relative offset to the fallback code
	// path executed following a transactional abort.

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

	/*
	    [chw] When a transaction successfully created, this function will return 
		  0xffffffff (i.e., _XBEGIN_STARTED), which is never a valid status
		  code for an abort transaction.
		  When a transaction aborts during execution, it will discards all 
		  register and memory updates and update the eax register with the 
		  status code of the aborted transaction, which can be used to
		  transfer control to a fallback handler.
	*/
	inline unsigned int _xbegin(void)
	{
	    unsigned status;
	    // [mfs] Should we be using _XBEGIN_STARTED here?
	    asm volatile("movl $0xffffffff, %%eax \n\t"
			 TSX_BEGIN " \n\t"
			 "movl %%eax, %0"
			 :"=r"(status)::"%eax", "memory");
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
	    asm volatile(TSX_END " \n\t":::"memory");
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
	    asm volatile(TSX_ABORT " \n\t":::"memory");
	}
}

#endif
