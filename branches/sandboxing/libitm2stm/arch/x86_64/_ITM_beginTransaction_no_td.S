//
//  Copyright (C) 2011
//  University of Rochester Department of Computer Science
//    and
//  Lehigh University Department of Computer Science and Engineering
// 
// License: Modified BSD
//          Please see the file LICENSE.RSTM for licensing information

// has the definitions for the checkpoint regiser offsets
#include "checkpoint.h"
// has the definitions for the _ITM_transaction and Scope offsets
#include "offsets.h"
#include "common.h"

        .text
	    .p2align 4,,15
	    .globl	_ITM_beginTransaction
	    ASM_DOT_TYPE(_ITM_beginTransaction, @function)
_ITM_beginTransaction:
.LFB1323:
	    ASM_DOT_CFI_STARTPROC
	    movq	%rbx, -24(%rsp) // callee saves RBX
	    movq	%r12, -8(%rsp)  // callee saves R12-R15
	    movl	%edi, %r12d     // r12d = uin43_t flags
	    ASM_DOT_CFI_OFFSET(12, -16)
	    ASM_DOT_CFI_OFFSET(3, -32)
	    movq	%rbp, -16(%rsp) // callee saves EBP
	    subq	$24, %rsp       // makes stack space
	    ASM_DOT_CFI_DEF_CFO_OFFSET(32)
	    ASM_DOT_CFI_OFFSET(6, -24)
	    call	_ITM_getTransaction
	    movq	%rax, %rbx      // RBX = _ITM_transaction* td
	    movq	TRANSACTION_INNER_(%rax), %rax
	    testq	%rax, %rax
	    jne	.check_scope_aborted
.get_scope:
	    movq	TRANSACTION_FREE_SCOPES_(%rbx), %rbp    // RBP = checkpoint
	    testq	%rbp, %rbp
	    je	.allocate_new_scope
.checkpoint:
	    movq	NODE_NEXT_(%rbp), %rax
	    movq	%rax, TRANSACTION_FREE_SCOPES_(%rbx)

        
        // store simple registers into the returned checkpoint
        movq    %r13, CHECKPOINT_R13_(%rbp)
        movq    %r14, CHECKPOINT_R14_(%rbp)
        movq    %r15, CHECKPOINT_R15_(%rbp)

        //  24(esp) == return ip...?
        //  16(esp) == r12
        //   8(esp) == rbp
        //    (esp) == rbx
        movq    (%rsp), %rcx
        movq    %rcx, CHECKPOINT_RBX_(%rbp)
        movq    8(%rsp), %rcx
        movq    %rcx, CHECKPOINT_RBP_(%rbp)
        movq    16(%rsp), %rcx
        movq    %rcx, CHECKPOINT_R12_(%rbp)
        movq    24(%rsp), %rcx
        movq    %rcx, CHECKPOINT_RIP_(%rbp)

        movq    %rsp, %rcx
        addq    $24, %rcx
        movq    %rcx, CHECKPOINT_RSP_(%rbp)

	    movl	%r12d, %edx // flags for enter
	    movq	%rbp, %rsi  // scope for enter
	    movq	%rbx, %rdi  // transaction for enter
	    call	_stm_itm2stm_transaction_enter
	    orl	$4, %eax
.exit:
	    movq	(%rsp), %rbx
	    movq	8(%rsp), %rbp
	    movq	16(%rsp), %r12
	    addq	$24, %rsp
	    .cfi_remember_state
	    .cfi_def_cfa_offset 8
	    ret
.check_scope_aborted:
	    .cfi_restore_state
	    cmpb	$0, SCOPE_ABORTED_(%rax)
	    je	.get_scope
	    movl	$16, %eax   // return a_abortTransaction
	    jmp	.exit
.allocate_new_scope:
	    movq	%rbx, %rdi
	    call	_stm_itm2stm_transaction_new_node
	    movq	%rax, %rbp
	    jmp	.checkpoint
        ASM_DOT_CFI_ENDPROC
.LFE1323:
	    ASM_DOT_SIZE(_ITM_beginTransaction, .-_ITM_beginTransaction)
