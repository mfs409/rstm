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
 *  This contains ASM implementations for _ITM_beginTransaction, and
 *  the associated _rstm_checkpoint_restore.
 */

#include "checkpoint.hpp"

#if defined(__x86_64__) && defined(__LP64__)

// describe the descriptor layout
#define DEPTH 0
#define INTX 8
#define RBP 16
#define RSP 24
#define RIP 32
#define RBX 40
#define R12 48
#define R13 56
#define R14 64
#define R15 72

/**
 *  The x86_64 _ITM_beginTransaction gets a checkpoint (or NULL in a nested
 *  context), initializes it, and calls the appropriate post-checkpoint
 *  function using a sibling call. We currently disregard the potential for
 *  useful flags in the varargs to _ITM_beginTransaction.
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
#ifdef STM_OS_SOLARIS
        movq    %fs:0, %rax
        movq    _ZN3stm4SelfE@tpoff(%rax), %rsi
#else
        movq    _ZN3stm4SelfE@gottpoff(%rip), %rsi
        movq    %fs:(%rsi), %rsi        // %rsi == tx
#endif
        movl    DEPTH(%rsi), %edx       // %edx == nesting_depth
        addl    $1, %edx                // if(++nesting_depth == 1)
        movl    %edx, DEPTH(%rsi)       //     goto nested
        cmpl    $1, %edx                //
        jne     _ITM_beginTransaction_nested

        // if we didn't jump, we need to checkpoint the sp, ip, and callee
        // saves

        movq    (%rsp), %rdx            // get the return address from
        movq    %rdx, RIP(%rsi)         // the stack

        movq    %rbp, RBP(%rsi)         // save callee saves
        movq    %rsp, RSP(%rsi)
        movq    %rbx, RBX(%rsi)
        movq    %r12, R12(%rsi)
        movq    %r13, R13(%rsi)
        movq    %r14, R14(%rsi)
        movq    %r15, R15(%rsi)

        // TODO: need to mark self as transactional with CAS/XCHG

        // TODO: need to update the tick info

        // call the per-algorithm begin function (i.e.,
        // TM_FASTCALL bool(*volatile tmbegin)()).  Note that we need
        // ordering for the read of this function pointer **after** the write to
        // tx->in_tx
        movq    _ZN3stm7tmbeginE, %rdx
        jmp     *%rdx

_ITM_beginTransaction_nested:
        // nested transactions take the instrumented path, but don't request
        // that any saving be done locally, because we don't support nested
        // abort/cancel at this point.
        movl    0x1, %eax // [mfs] do we need this?  Verify for 64-bit and
                      //       32-bit
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

#ifdef STM_OS_SOLARIS
        movq    %fs:0, %rax
        movq    _ZN3stm4SelfE@tpoff(%rax), %rsi
#else
        movq    _ZN3stm4SelfE@gottpoff(%rip), %rsi
        movq    %fs:(%rsi), %rsi        // %rsi == tx
#endif

        movq    RBP(%rsi), %rbp         // restore the callee saves
        movq    RSP(%rsi), %rsp
        movq    RBX(%rsi), %rbx
        movq    R12(%rsi), %r12
        movq    R13(%rsi), %r13
        movq    R14(%rsi), %r14
        movq    R15(%rsi), %r15

        movq    RIP(%rsi), %rax         // put the return address on
        movq    %rax, (%rsp)            // the stack

        movq    _ZN3stm7tmbeginE, %rdx  //
        jmp     *%rdx                   // call the continuation as a sibling
                                        // call.

        ASM_DOT_CFI_ENDPROC
        ASM_DOT_SIZE(_rstm_restore_checkpoint, .-_rstm_restore_checkpoint)

#elif defined(__x64_64__)
# error No checkpoint code designed for x32 yet.
#elif defined(__i386__)

// describe the checkpoint offsets
#define DEPTH 0
#define INTX 4
#define EBP 8
#define ESP 12
#define EIP 16
#define EBX 20
#define ESI 24
#define EDI 28

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

/**
 *  The SPARC 64-bit checkpointing functions are modified from Dave Dice's
 *  custom FIsetjmp and FIlongjmp, which are distributed as part of SkySTM.
 *  Note that those files are protected by a GPL license.  The actual files
 *  are in alt-license, for reference.
 */

// sparc calling convention:

// %i6 is frame pointer
// %i7 is (return address - 8)... I think it is actually the return address of the parent, from when it was jal'd
// %o6 is the stack pointer
// %o7 is address of call instruction

// NB: the i registers and o0 should not be changed

// http://www.sics.se/~psm/sparcstack.html
// http://en.wikipedia.org/wiki/Calling_convention#SPARC

// NB: we never called 'save', so there is no register window roll... this
// means it is OK to trash my O registers, but I should leave my I registers
// untouched in this code.  Note, too, that I'm allowed to trash my L
// registers, because the caller should have assumed the Ls would get trashed

#ifdef STM_BITS_32
    .align  64
    .globl  _ITM_beginTransaction
    .type   _ITM_beginTransaction, #function
    .register %g3, #scratch
    .register %g2, #scratch
_ITM_beginTransaction:

    // Step 1: need to get the TLS information into a usable register.  Note
    // that this code is based on an assembler dump for:
    // {TxThread* tx = Self; return tx;}
    or    %g0, %o7, %g1 // save %o7 to %g1
    sethi %tie_hi22(_ZN3stm4SelfE),%g3 // get high bits of self into g3

.framemid_ITM_beginTransaction_:
    rd    %pc, %o7 // get pc into o7
    sethi %pc22(_GLOBAL_OFFSET_TABLE_-(.framemid_ITM_beginTransaction_-.)),%g2 // get lookup bits of GOT into g2
    add   %g3, %tie_lo10(_ZN3stm4SelfE), %g3 // g3 has all bits of self
    add   %g2, %pc10(_GLOBAL_OFFSET_TABLE_-(.framemid_ITM_beginTransaction_-.)),%g2 // g2 has entire GOT address
    add   %g2, %o7, %g2 // g2 holds pc plus got addr
    or    %g0, %g1, %o7 // o7 restored to original value
    ld    [%g2+%g3], %g1, %tie_ld(_ZN3stm4SelfE) // o1 holds address of most of Self
    add   %g7, %g1, %g1,%tie_add(_ZN3stm4SelfE) // g1 adds in the g7 magic
    ld    [%g1], %g1 // dereference to get the pointer to the descriptor into g1

    // now that %g1 has the pointer to the descriptor, we can do this code:
    //  if (++tx->nesting_depth > 1) return;
    ld  [%g1], %g2 // %g2 holds the value of the nesting depth
    add %g2, 1, %g3 // nesting_depth++
    cmp %g3, 1
    bgu,pn  %icc, .frameearlyexit_ITM_beginTransaction_
    st  %g3, [%g1]

    // now it's time to do tx->in_tx = 1; WBR.  We can do this with a stb instruction
    or %g0, 1, %g2
    stb %g2, [%g1+4] ! volatile

    // Save SP, FP, PC (return address)
    st  %i7, [%g1+(4*15)]
    st  %fp, [%g1+(4*16)]
    st  %sp, [%g1+(4*17)]
    add %o7, 8, %g2
    st  %g2, [%g1+(4*19)]       // return address is caller + 8

    // the tick stuff should go here

    // load the function pointer from TLS
    sethi  %hi(_ZN3stm7tmbeginE), %g2
    ld  [%g2+%lo(_ZN3stm7tmbeginE)],%g2 ! volatile
    jmpl    %g2,%g0
    nop
.frameearlyexit_ITM_beginTransaction_:
    retl
    nop
    .size   _ITM_beginTransaction, .-_ITM_beginTransaction






    .globl  _rstm_restore_checkpoint
    .type   _rstm_restore_checkpoint, #function
    .register %g3, #scratch
    .register %g2, #scratch
  _rstm_restore_checkpoint:

    // Step 1: need to get the TLS information into a usable register.  Note
    // that this code is based on an assembler dump for:
    // {TxThread* tx = Self; return tx;}
    or    %g0, %o7, %g1 // save %o7 to %g1
    sethi %tie_hi22(_ZN3stm4SelfE),%g3 // get high bits of self into g3

.framemid_rstm_restore_checkpoint_:
    rd    %pc, %o7 // get pc into o7
    sethi %pc22(_GLOBAL_OFFSET_TABLE_-(.framemid_rstm_restore_checkpoint_-.)),%g2 // get lookup bits of GOT into g2
    add   %g3, %tie_lo10(_ZN3stm4SelfE), %g3 // g3 has all bits of self
    add   %g2, %pc10(_GLOBAL_OFFSET_TABLE_-(.framemid_rstm_restore_checkpoint_-.)),%g2 // o4 has entire GOT address
    add   %g2, %o7, %g2 // g2 holds pc plus got addr
    or    %g0, %g1, %o7 // o7 restored to original value
    ld    [%g2+%g3], %g1, %tie_ld(_ZN3stm4SelfE) // g1 holds address of most of Self
    add   %g7, %g1, %g1,%tie_add(_ZN3stm4SelfE) // g1 adds in the g7 magic
    ld    [%g1], %g1 // dereference to get the pointer to the descriptor into o)

    // %g1 has a pointer to the descriptor

    // ASSERT fp <= jmpbuf->fp
    // Trim the frames as needed with RESTORE
    // while jmpbuf->fp != fp : RESTORE ;
    ld  [%g1+(4*16)], %g2
  1:cmp %g2, %fp                // In originating frame ?
    be  %xcc, 2f
    nop
    ba 1b
    restore                     // Trim one frame
  2:nop
    // We're back in the originating frame
    ld  [%g1+(4*15)], %i7
    // fp has already been restored via RESTORE, so we need to restore only SP and PC.
    // Recall that sp EQU o6 and fp EQU i6, so restoring i6, above, is probably redundant.
    ld  [%g1+(4*17)], %sp
    ld  [%g1+(4*19)], %o7

    // load the function pointer from TLS
    sethi  %hi(_ZN3stm7tmbeginE), %g2
    ld     [%g2+%lo(_ZN3stm7tmbeginE)],%g2 ! volatile
    jmpl   %g2,%g0
    nop
    .size   _rstm_restore_checkpoint, .-_rstm_restore_checkpoint

#else
    .align  64
    .globl  _ITM_beginTransaction
    .type   _ITM_beginTransaction, #function
    .register %g3, #scratch
    .register %g2, #scratch
_ITM_beginTransaction:

    // Step 1: need to get the TLS information into a usable register.  Note
    // that this code is based on an assembler dump for:
    // {TxThread* tx = Self; return tx;}
    or    %g0, %o7, %g1 // save %o7 to %g1... o7 is address of calling instruction
    sethi %tie_hi22(_ZN3stm4SelfE),%g3 // get high bits of self into g3

.framemid_ITM_beginTransaction_:
    rd    %pc, %o7 // get pc into o7
    sethi %pc22(_GLOBAL_OFFSET_TABLE_-(.framemid_ITM_beginTransaction_-.)),%g2 // get lookup bits of GOT into g2
    add   %g3, %tie_lo10(_ZN3stm4SelfE), %g3 // g3 has all bits of self
    add   %g2, %pc10(_GLOBAL_OFFSET_TABLE_-(.framemid_ITM_beginTransaction_-.)),%g2 // g2 has entire GOT address
    add   %g2, %o7, %g2 // g2 holds pc plus got addr
    or    %g0, %g1, %o7 // o7 restored to original value
    ldx   [%g2+%g3], %g1, %tie_ldx(_ZN3stm4SelfE) // o1 holds address of most of Self
    add   %g7, %g1, %g1,%tie_add(_ZN3stm4SelfE) // o0 adds in the g7 magic
    ldx   [%g1], %o0 // dereference to get the pointer to the descriptor into o0

    // now that %o0 has the pointer to the descriptor, we can do this code:
    //  if (++tx->nesting_depth > 1) return;
    ldx [%o0], %o4 // %o4 holds the value of the nesting depth
    add %o4, 1, %o5 // nesting_depth++
    cmp %o5, 1
    bgu,pn  %icc, .frameearlyexit_ITM_beginTransaction_
    st  %o5, [%o0]

    // now it's time to do tx->in_tx = 1; WBR.  We can do this with a stb instruction
    or %g0, 1, %o4
    stb %o4, [%o0+4] ! volatile

    // Save SP, FP, PC (return address)
    stx %i7, [%o0+(8*15)]
    stx %fp, [%o0+(8*16)]
    stx %sp, [%o0+(8*17)]
    add %o7, 8, %g1
    stx %g1, [%o0+(8*19)]       // return address

    // the tick stuff should go here

    // load the function pointer from TLS
    sethi  %h44(_ZN3stm7tmbeginE), %g2
    or  %g2,%m44(_ZN3stm7tmbeginE),%g3
    sllx    %g3,12,%g3
    ldx [%g3+%l44(_ZN3stm7tmbeginE)],%g2 ! volatile
    jmpl    %g2,%g0
    nop
.frameearlyexit_ITM_beginTransaction_:
    retl
    nop
    .size   _ITM_beginTransaction, .-_ITM_beginTransaction






    .globl  _rstm_restore_checkpoint
    .type   _rstm_restore_checkpoint, #function
    .register %g3, #scratch
    .register %g2, #scratch
  _rstm_restore_checkpoint:

    // Step 1: need to get the TLS information into a usable register.  Note
    // that this code is based on an assembler dump for:
    // {TxThread* tx = Self; return tx;}
    or    %g0, %o7, %g1 // save %o7 to %g1
    sethi %tie_hi22(_ZN3stm4SelfE),%g3 // get high bits of self into g3

.framemid_rstm_restore_checkpoint_:
    rd    %pc, %o7 // get pc into o7
    sethi %pc22(_GLOBAL_OFFSET_TABLE_-(.framemid_rstm_restore_checkpoint_-.)),%g2 // get lookup bits of GOT into g2
    add   %g3, %tie_lo10(_ZN3stm4SelfE), %g3 // g3 has all bits of self
    add   %g2, %pc10(_GLOBAL_OFFSET_TABLE_-(.framemid_rstm_restore_checkpoint_-.)),%g2 // o4 has entire GOT address
    add   %g2, %o7, %g2 // g2 holds pc plus got addr
    or    %g0, %g1, %o7 // o7 restored to original value
    ldx   [%g2+%g3], %g1, %tie_ldx(_ZN3stm4SelfE) // g1 holds address of most of Self
    add   %g7, %g1, %g1,%tie_add(_ZN3stm4SelfE) // g1 adds in the g7 magic
    ldx   [%g1], %g1 // dereference to get the pointer to the descriptor into o)

    // %g1 has a pointer to the descriptor

    // ASSERT fp <= jmpbuf->fp
    // Trim the frames as needed with RESTORE
    // while jmpbuf->fp != fp : RESTORE ;
    ldx [%g1+(8*16)], %g2
  1:cmp %g2, %fp                // In originating frame ?
    be  %xcc, 2f
    nop
    ba 1b
    restore                     // Trim one frame
  2:nop
    // We're back in the originating frame
    ldx [%g1+(8*15)], %i7
    // fp has already been restored via RESTORE, so we need to restore only SP and PC.
    // Recall that sp EQU o6 and fp EQU i6, so restoring i6, above, is probably redundant.
    ldx [%g1+(8*17)], %sp
    ldx [%g1+(8*19)], %o7

    // load the function pointer from TLS
    sethi  %h44(_ZN3stm7tmbeginE), %g2
    or  %g2,%m44(_ZN3stm7tmbeginE),%g3
    sllx    %g3,12,%g3
    ldx [%g3+%l44(_ZN3stm7tmbeginE)],%g2 ! volatile
    jmpl    %g2,%g0
    nop
    .size   _rstm_restore_checkpoint, .-_rstm_restore_checkpoint
#endif

#else
# error No checkpoint code for your architecture (something's _really_ wrong).
#endif
