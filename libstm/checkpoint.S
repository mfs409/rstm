/** -*- asm -*-
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

/**
 *  This contains ASM implementations for _ITM_beginTransaction, and
 *  the associated _rstm_checkpoint_restore.
 */
        
#include "checkpoint.hpp"
        
#if defined(__x86_64__) && defined(__LP64__)

// describe the descriptor layout
#define DEPTH 0
#define INTX 8
#define RBP 8
#define RSP 16
#define RIP 24
#define RBX 32
#define R12 40
#define R13 48
#define R14 56
#define R15 64
#define RDI 72
        
/**
 *  The x86_64 _ITM_beginTransaction gets a checkpoint (or NULL in a nested
 *  context), initializes it, and calls the appropriate post-checkpoint function
 *  using a sibling call. We currently disregard the potential for useful flags
 *  in the varargs to _ITM_beginTransaction.
 * 
 *  - %rdi: flags
 */
        .text
        .p2align 4,,15
        .globl _ITM_beginTransaction
        ASM_DOT_TYPE(_ITM_beginTransaction, @function)
_ITM_beginTransaction:
        ASM_DOT_CFI_STARTPROC
        // Load the TLS descriptor and bump the nesting depth---if we're nested
        // we can just return. I pretty much just copied this wholesale from a
        // C implementation, except that I messed with the instruction
        // scheduling to preserve registers that I need for the checkpoint.
        movq    _ZN3stm4SelfE@gottpoff(%rip), %rsi
        movq    %fs:(%rsi), %rsi        // %rsi == tx
        movl    DEPTH(%rsi), %edx       // %edx == nesting_depth
        addl    $1, %edx                // if(++nesting_depth == 1)
        movl    %edx, DEPTH(%rsi)       //     goto nested
        cmpl    $1, %edx                // 
        jne     _ITM_beginTransaction_nested
        
        movq    (%rsp), %rdx            // grab the return address
        movq    %rdx, RIP(%rsi)         // from the stack
        
        movq    %rbp, RBP(%rsi)
        movq    %rsp, RSP(%rsi)
        movq    %rbx, RBX(%rsi)
        movq    %r12, R12(%rsi)
        movq    %r13, R13(%rsi)
        movq    %r14, R14(%rsi)
        movq    %r15, R15(%rsi)
        movq    %rdi, RDI(%rsi)         // save flags for abort path

        // stm::tm_begin(%rdi=flags, %rsi=TX*, %rdx=return flag)
        movl    $4, %edx
        jmp     _ZN3stm8tm_beginEjPNS_2TXEj
        
_ITM_beginTransaction_nested:
        // nested transactions take the instrumented path, but don't request
        // that any saving be done locally, because we don't support nested
        // abort/cancel at this point.
        movl    0x1, %eax
        ret
        
        ASM_DOT_CFI_ENDPROC
        ASM_DOT_SIZE(_ITM_beginTransaction, .-_ITM_beginTransaction)

/**
 *  The x86_64 restore_checkpoint just extracts the saved registers from the
 *  checkpoint, sticks the return address on the stack, and re-calls tm_begin.
 * 
 *  - %rdi: continuation (type of tm_begin_t)
 *  - %rsi: tx
 */
        .text
        .p2align 4,,15
        .globl _rstm_restore_checkpoint
        ASM_DOT_TYPE(_rstm_restore_checkpoint, @function)
_rstm_restore_checkpoint:
        ASM_DOT_CFI_STARTPROC

        movq    RBP(%rsi), %rbp
        movq    RSP(%rsi), %rsp
        movq    RBX(%rsi), %rbx
        movq    R12(%rsi), %r12
        movq    R13(%rsi), %r13
        movq    R14(%rsi), %r14
        movq    R15(%rsi), %r15

        movq    RIP(%rsi), %rax         // put the return address on
        movq    %rax, (%rsp)            // the stack

        movq    %rdi, %rax              // restore the flags and call the 
        movq    RDI(%rsi), %rdi         // continuation using a sibling call

        jmp     *%rax
        
        ASM_DOT_CFI_ENDPROC
        ASM_DOT_SIZE(_rstm_restore_checkpoint, .-_rstm_restore_checkpoint)

#elif defined(__x64_64__)
# error No checkpoint code designed for x32 yet.
#elif defined(__i386__)
        
// describe the checkpoint offsets
#define DEPTH 0
#define INTX 8
#define EBP 4
#define ESP 8
#define EIP 12
#define EBX 16
#define ESI 20
#define EDI 24
#define EAX 28 // not callee-saves, but we use it to remember flags

/**
 *  The i386 _ITM_beginTransaction gets a checkpoint (or NULL in a nested
 *  context), initializes it, and calls the appropriate post-checkpoint function
 *  using a sibling call (rstm::post_checkpoint's type must match
 *  _ITM_beginTransaction). The sibling call makes the varargs work.
 * 
 *  Note that the ITM_REGPARM calling convention on _ITM_beginTransaction is
 *  ignored because it is a varargs function. This means that even the first
 *  parameter (flags) is passed on the stack, which is fine but means we have to
 *  do some work to prepare for _rstm_pre_checkpoint.
 * 
 *  -  (%esp): return address
 *  - 4(%esp): flags
 */
        .text        
        .p2align 4,,15
        .globl _ITM_beginTransaction
        ASM_DOT_TYPE(_ITM_beginTransaction, @function)
_ITM_beginTransaction:
        ASM_DOT_CFI_STARTPROC
        
        // At the moment, RSTM doesn't support nested aborts. We get the TX*
        // and bump the depth, and only checkpoint the outermost transaction.
        movl    _ZN3stm4SelfE@indntpoff, %edx
        movl    %gs:(%edx), %edx        // %edx holds the descriptor
        movl    DEPTH(%edx), %eax       // %eax == nesting_depth
        addl    $1, %eax                // if(++nesting_depth == 1)
        movl    %eax, DEPTH(%edx)       //     goto nested
        cmpl    $1, %eax                //
        jne     _ITM_beginTransaction_nested

        // if we didn't jump, we need to checkpoint the sp, ip, and callee
        // saves
        
        movl    (%esp), %eax            // get the return address from
        movl    %eax, EIP(%edx)         // the stack

        movl    %esp, ESP(%edx)         // save callee saves
        movl    %ebp, EBP(%edx)
        movl    %ebx, EBX(%edx)
        movl    %esi, ESI(%edx)
        movl    %edi, EDI(%edx)

        // Adaptivity now requires us to do the following:

        // mark self as transactional:
        //
        // casptr(&tx->in_tx, 0, 1);
        movl    $0x1, %ecx
        xor     %eax, %eax
        // [mfs] We could do a test and set instead...
        lock cmpxchg %cl, INTX(%edx)

        // Update tick info:
        // [mfs] this is not done yet, and needs to be added!
        //
        // if (tx->end_txn_time)
        //     tx->total_nontxn_time += (tick() - tx->end_txn_time);

        
        // call the per-algorithm begin function (i.e.,
        // TM_FASTCALL bool(*volatile tmbegin)()).  Note that we need
	// ordering for the read of this function pointer **after** the write to
	// tx->in_tx
        movl    _ZN3stm7tmbeginE, %eax
        jmp     *%eax

_ITM_beginTransaction_nested:
        movl    0x1, %eax
        ret

        ASM_DOT_CFI_ENDPROC
        ASM_DOT_SIZE(_ITM_beginTransaction, .-_ITM_beginTransaction)

/**
 *  The i386 restore_checkpoint just extracts the saved registers from the
 *  checkpoint, sticks the return address on the stack, and returns.
 * 
 *  - 4(%esp): the continuation
 *  - 8(%esp): the tx
 */
        .text
        .p2align 4,,15
        .globl _rstm_restore_checkpoint
        ASM_DOT_TYPE(_rstm_restore_checkpoint, @function)
_rstm_restore_checkpoint:
        ASM_DOT_CFI_STARTPROC

        movl    _ZN3stm4SelfE@indntpoff, %edx
        movl    %gs:(%edx), %edx        // %edx holds the descriptor
        
        movl    ESP(%edx), %esp         // restore the callee saves
        movl    EBP(%edx), %ebp
        movl    EBX(%edx), %ebx
        movl    ESI(%edx), %esi
        movl    EDI(%edx), %edi
        
        movl    EIP(%edx), %eax         // restore the return address
        movl    %eax, (%esp)

        // at this point, the stack frame is indistinguishable from if we had
	// called _ITM_beginTransaction.  Since we never un-set in_tx, now we
	// can just call the function pointer to do per-algorithm begin code
        
        movl    _ZN3stm7tmbeginE, %eax  //
        jmp     *%eax                   // call the continuation as a sibling
                                        // call. 
                
        ASM_DOT_CFI_ENDPROC
        ASM_DOT_SIZE(_rstm_restore_checkpoint, .-_rstm_restore_checkpoint)

#elif defined(__sparc)
# error No checkpoint code yet for SPARC.
#else
# error No checkpoint code for your architecture (something's _really_ wrong).
#endif
