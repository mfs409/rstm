/** -*- asm -*-
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

/// This contains ASM implementations for _ITM_beginTransaction, and
/// the associated _rstm_checkpoint_restore.
        
#include "checkpoint.hpp"
        
#if defined(__x86_64__) && defined(__LP64__)

// describe the checkpoint layout
#define RBP 0
#define RSP 8
#define RIP 16
#define RBX 24
#define R12 32
#define R13 40
#define R14 48
#define R15 56
        
///
/// The x86_64 _ITM_beginTransaction gets a checkpoint (or NULL in a nested
/// context), initializes it, and calls the appropriate post-checkpoint function
/// using a sibling call (rstm::post_checkpoint's type must match
/// _ITM_beginTransaction). The sibling call makes the varargs work.
///
/// - %rdi: flags
///
        .text
        /// .p2align 4,,15
        .globl __ITM_beginTransaction
        ASM_DOT_TYPE(__ITM_beginTransaction, @function)
__ITM_beginTransaction:
        ASM_DOT_CFI_STARTPROC
#ifdef __APPLE__                              // apple has stack aligment issues
        pushq   %rsp
        andq    $0xFFFFFFFFFFFFFFF0, %rsp
#endif
        call    _rstm_pre_checkpoint            // get the checkpoint (or NULL
#ifdef __APPLE__
        popq    %rsp
#endif
        testq   %rax, %rax                      // (if this is a nested begin)
        je      __ITM_beginTransaction_nested
        
        movq    (%rsp), %rcx                    // grab the return address
        movq    %rcx, RIP(%rax)                 // from the stack
        
        movq    %rbp, RBP(%rax)
        movq    %rsp, RSP(%rax)
        movq    %rbx, RBX(%rax)
        movq    %r12, R12(%rax)
        movq    %r13, R13(%rax)
        movq    %r14, R14(%rax)
        movq    %r15, R15(%rax)        

        jmp     _rstm_post_checkpoint           // jmp == sibling call
__ITM_beginTransaction_nested:
        jmp     _rstm_post_checkpoint_nested
        
        ASM_DOT_CFI_ENDPROC
        ASM_DOT_SIZE(__ITM_beginTransaction, .-__ITM_beginTransaction)

///
/// The x86_64 restore_checkpoint just extracts the saved registers from the
/// checkpoint, sticks the return address on the stack, and returns.
///
/// - %rdi: checkpoint
/// - %esi: return value
///
        .text
        .p2align 4,,15
        .globl _rstm_restore_checkpoint
        ASM_DOT_TYPE(_rstm_restore_checkpoint, @function)
_rstm_restore_checkpoint:
        ASM_DOT_CFI_STARTPROC

        movl    %esi, %eax                      // prepare the return value

        movq    RBP(%rdi), %rbp
        movq    RSP(%rdi), %rsp
        movq    RBX(%rdi), %rbx
        movq    R12(%rdi), %r12
        movq    R13(%rdi), %r13
        movq    R14(%rdi), %r14
        movq    R15(%rdi), %r15

        movq    RIP(%rdi), %rdi                 // put the return address on
        movq    %rdi, (%rsp)                    // the stack, and return
        ret
        
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
