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
        .globl _ITM_beginTransaction
        ASM_DOT_TYPE(_ITM_beginTransaction, @function)
_ITM_beginTransaction:
        .cfi_startproc

        leal    4(%esp), %eax
        subl    $40, %esp
        .cfi_def_cfa_offset 44
        movl    40(%esp), %edx
        movl    %edx, CHECKPOINT_EIP_(%esp)
        movl    %eax, CHECKPOINT_ESP_(%esp)
        movl    %ebx, CHECKPOINT_EBX_(%esp)
        movl    %esi, CHECKPOINT_ESI_(%esp)
        movl    %edi, CHECKPOINT_EDI_(%esp)
        movl    %ebp, CHECKPOINT_EBP_(%esp)
        leal    (%esp), %edx
        call    _ZN16_ITM_transaction11enter_no_tdEjPPv
        addl    $40, %esp
        .cfi_def_cfa_offset 4
        ret
        .cfi_endproc
        ASM_DOT_SIZE(_ITM_beginTransaction, .-_ITM_beginTransaction)


// CHECKPOINT_EDX_, CHECKPOINT_ECX_, CHECKPOINT_ESP_

