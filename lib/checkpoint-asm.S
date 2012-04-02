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
        movq	_ZN3stm4SelfE@gottpoff(%rip), %rsi
	    movq	%fs:(%rsi), %rsi        // %rsi == tx
	    movl	DEPTH(%rsi), %edx       // %edx == nesting_depth
	    addl	$1, %edx                // if(++nesting_depth == 1)
	    movl	%edx, DEPTH(%rsi)       //     goto nested
	    cmpl	$1, %edx                // 
        jne     _ITM_beginTransaction_nested
        
        movq    (%rsp), %rcx            // grab the return address
        movq    %rcx, RIP(%rsi)         // from the stack
        
        movq    %rbp, RBP(%rsi)
        movq    %rsp, RSP(%rsi)
        movq    %rbx, RBX(%rsi)
        movq    %r12, R12(%rsi)
        movq    %r13, R13(%rsi)
        movq    %r14, R14(%rsi)
        movq    %r15, R15(%rsi)
        movq    %rdi, RDI(%rsi)         // save flags for abort path

        // stm::tm_begin(%edi=flags, %esi=TX*)
        jmp     _ZN3stm8tm_beginEjPNS_2TXE

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
 *  - %rdi: checkpoint
 *  - %esi: return value
 */
        .text
        .p2align 4,,15
        .globl _rstm_restore_checkpoint
        ASM_DOT_TYPE(_rstm_restore_checkpoint, @function)
_rstm_restore_checkpoint:
        ASM_DOT_CFI_STARTPROC

        movq    RBP(%rdi), %rbp
        movq    RSP(%rdi), %rsp
        movq    RBX(%rdi), %rbx
        movq    R12(%rdi), %r12
        movq    R13(%rdi), %r13
        movq    R14(%rdi), %r14
        movq    R15(%rdi), %r15

        movq    RIP(%rdi), %rax                 // put the return address on
        movq    %rax, (%rsp)                    // the stack

        movq    RDI(%rdi), %rdi                 // restore the flags
        jmp     _rstm_post_restart              // and reenter the transaction
                                                // as a sibling call
        
        ASM_DOT_CFI_ENDPROC
        ASM_DOT_SIZE(_rstm_restore_checkpoint, .-_rstm_restore_checkpoint)

#elif defined(__x64_64__)
# error No checkpoint code designed for x32 yet.
#elif defined(__i386__)
        
// describe the checkpoint offsets
#define EBP 0
#define ESP 4
#define EIP 8
#define EBX 12
#define ESI 16
#define EDI 20
#define EAX 24 // not callee-saves, but we use it to remember flags

///
/// The i386 _ITM_beginTransaction gets a checkpoint (or NULL in a nested
/// context), initializes it, and calls the appropriate post-checkpoint function
/// using a sibling call (rstm::post_checkpoint's type must match
/// _ITM_beginTransaction). The sibling call makes the varargs work.
///
/// Note that the ITM_REGPARM calling convention on _ITM_beginTransaction is
/// ignored because it is a varargs function. This means that even the first
/// parameter (flags) is passed on the stack, which is fine but means we have to
/// do some work to prepare for _rstm_pre_checkpoint.
///
/// -  (%esp): return address
/// - 4(%esp): flags
/// - X(%esp): ... (varargs param to _ITM_beginTransaction)
///
        .text        
        .p2align 4,,15
        .globl _ITM_beginTransaction
        ASM_DOT_TYPE(_ITM_beginTransaction, @function)
_ITM_beginTransaction:
        ASM_DOT_CFI_STARTPROC
        
        movl    4(%esp), %eax                   // get a checkpoint (or NULL in
#ifdef __APPLE__
        pushl   %esp                          // apple has stack aligment issues
        andl    $0xFFFFFFF0, %esp
#endif
        call    _rstm_pre_checkpoint            // a nested contet)---uses
#ifdef __APPLE__
        popl    %esp
#endif
        testl   %eax, %eax                      // regparm(1) to pass flags to 
        je      _ITM_beginTransaction_nested   // _rstm_pre_checkpoint.

        movl    (%esp), %ecx                    // get the return address from
        movl    %ecx, EIP(%eax)                 // the stack

        movl    4(%esp), %ecx                   // remember flags in case we
        movl    %ecx, EAX(%eax)                 // are aborted

        movl    %esp, ESP(%eax)
        movl    %ebp, EBP(%eax)
        movl    %ebx, EBX(%eax)
        movl    %esi, ESI(%eax)
        movl    %edi, EDI(%eax)
        
        jmp     _rstm_post_checkpoint           // jmp == sibling call, which
_ITM_beginTransaction_nested:                  // is necessary to make varargs
        jmp     _rstm_post_checkpoint_nested    // work

        ASM_DOT_CFI_ENDPROC
        ASM_DOT_SIZE(_ITM_beginTransaction, .-_ITM_beginTransaction)

///
/// The i386 restore_checkpoint just extracts the saved registers from the
/// checkpoint, sticks the return address on the stack, and returns.
///
/// - 4(%esp): the checkpoint_t*
/// - 8(%esp): the return value
///
        .text
        .p2align 4,,15
        .globl _rstm_restore_checkpoint
        ASM_DOT_TYPE(_rstm_restore_checkpoint, @function)
_rstm_restore_checkpoint:
        ASM_DOT_CFI_STARTPROC

        // this doesn't work like longjmp, we don't pass in a return value, we
        // reuse the flags that were originally passed to us
        // movl    0x8(%esp), %eax                 // setup the return value
        movl    0x4(%esp), %ecx                 // grab the buffer

        movl    ESP(%ecx), %esp
        movl    EBP(%ecx), %ebp
        movl    EBX(%ecx), %ebx
        movl    ESI(%ecx), %esi
        movl    EDI(%ecx), %edi

        // we're executing in the same stack fram as _ITM_begin was, fix some
        // of the stack that we /know/ was there when we were checkpointed.
        movl    EAX(%ecx), %eax                 // restore the flags
        movl    %eax, 4(%esp)
        
        movl    EIP(%ecx), %ecx                 // restore the return address
        movl    %ecx, (%esp)

        jmp     _rstm_post_restart              // reenter the transaction
                
        ASM_DOT_CFI_ENDPROC
        ASM_DOT_SIZE(_rstm_restore_checkpoint, .-_rstm_restore_checkpoint)

#elif defined(__sparc)
# error No checkpoint code yet for SPARC.
#else
# error No checkpoint code for your architecture (something's _really_ wrong).
#endif
