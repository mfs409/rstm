!
! Copyright (c) 2009-2010, Oracle and/or its affiliates.
!
! All rights reserved.
!
! Sun Microsystems, Inc. has intellectual property rights relating to
! technology embodied in the product that is described in this
! document. In particular, and without limitation, these intellectual
! property rights may include one or more of the U.S. patents listed
! at http://www.sun.com/patents and one or more additional patents or
! pending patent applications in the U.S. and in other countries.
!
! U.S. Government Rights - Commercial software. Government users are
! subject to the Sun Microsystems, Inc. standard license agreement
! and applicable provisions of the FAR and its supplements.
!
! Use is subject to license terms. Sun, Sun Microsystems and the Sun
! logo are trademarks or registered trademarks of Sun Microsystems,
! Inc. in the U.S. and other countries. All SPARC trademarks are used
! under license and are trademarks or registered trademarks of SPARC
! International, Inc. in the U.S. and other countries.
!
! ----------------------------------------------------------------------
!
! This file is part of the Hybrid Transactional Memory Library
! (SkySTM Library) developed and maintained by Yossi Lev, Virendra
! Marathe and Dan Nussbaum of the Scalable Synchronization Research
! Group at Sun Microsystems Laboratories
! (http://research.sun.com/scalable/).
!
! Please send email to skystm-feedback@sun.com with feedback,
! questions, or to request future announcements about SkySTM.
!
! ----------------------------------------------------------------------
!
! The SkySTM Library is available for use and modification under the
! terms of version 2 of the GNU General Public License.  The GNU
! General Public License is contained in the file
! $(SKSTMLIB)/SOFTWARELICENSE.txt.
!
! The SkySTM Library can be redistributed and/or modified under the
! terms of version 2 of the GNU General Public License as published
! by the Free Software Foundation.
!
! In addition, we ask that you adhere to the following requests if
! you use this software:
!
!  * If your use of this software contributes to a published paper,
!    we request that you (1) cite our summary paper that appears on
!    our website
!    (http://research.sun.com/scalable/pubs/TRANSACT2009-ScalableSTMAnatomy.pdf)
!    and (2) e-mail a citation for your published paper to
!    skystm-feedback@sun.com.
!
!  * If you redistribute derivatives of this software, we request
!    that you notify us and either (1) ask people to register with us
!    at skystm-feedback@sun.com or (2) collect registration
!    information and periodically send it to us.
!
! The SkySTM Library is distributed in the hope that it will be
! useful, but WITHOUT ANY WARRANTY; without even the implied warranty
! of MERCHANTABILITY, NON-INFRINGEMENT or FITNESS FOR A PARTICULAR
! PURPOSE.  See the GNU General Public License for more details.
!

!! Copyright (C) Sun Microsystems, Inc. 2006.  All Rights Reserved. 
!!
!! FIsetjmp() and FIlongjmp() implement a restricted form of setjmp() 
!! and longjmp() for use in an STM implementation where we want to longjmp() back
!! to the start of the txn to retry on abort.  The solaris longjmp() implementation
!! uses ST_FLUSH_WINDOWS trap to flush and invalidate the register windows.
!! While correct, this incurs a considerable penalty because of trap and, depending
!! on the call graph, the deferred (and likely unnecessary) fill traps.  
!! Our variant, which is less general, simply loops, executing RESTORE to trim
!! the call stack back to the original setjmp() scope.  The only issue is that it
!! may confuse the RAS.  
!!
!! Beware that we don't run on-stack dtors in C++ environments.
!! As noted above, our setjmp-longjmp is specialized.  It can't be used
!! for instance, to implement coroutines or user-mode stack switching.  
!! Also, it's assumed transactions are lexically scoped.  
!! See the comments in TL.c regarding the usage of TXSTART and LTXSTART.
!!
!! In such an environment the retry loop is implicit.  
!! In FIsetjmp() we could consider poisoning the return address to point
!! to a specialized thunk.  The thunk could reside in the jmpbuf and, when
!! executed, mark the jmpbuf as invalid.  This would ensure the setjmp() frame
!! stays in-scope and active during a transaction.  That is, we could detect 
!! and trap "up level" or "up stack" longjmps()  
!!
!! See also
!! --   TL2 RELEASENOTES.txt
!! --   Solaris usr/src/lib/libc/sparcv9/gen/setjmp.s
!! --   parasitic leaf routines
!! 
!! use SunCC pragmas and GCC __attribute__ settings to inform the compiler that the
!! routine is non-standard that auto variables should be considered volatile.  
!! #pragma unknown_control_flow (setjmp)
!! __attribute__ ((__noreturn__)) for longjmp()
!! __attribute__ ((returns_twice)) for setjmp()
!! 

    .text

    .register %g2, #scratch
    .register %g3, #scratch

    .align  4
    .globl GetFP
    .type  GetFP, #function
  GetFP:
     retl
     mov %fp, %o0
     .size GetFP, .-GetFP

    .align  64
    .globl  FIsetjmp_Conservative
    .type   FIsetjmp_Conservative, #function
  FIsetjmp_Conservative:
    !! First, save the L and I registers into the buf
    !! CF solaris setjmp(), which saves fp and sp
    stx %l0, [%o0+(8*0)] 
    stx %l1, [%o0+(8*1)] 
    stx %l2, [%o0+(8*2)] 
    stx %l3, [%o0+(8*3)] 
    stx %l4, [%o0+(8*4)] 
    stx %l5, [%o0+(8*5)] 
    stx %l6, [%o0+(8*6)] 
    stx %l7, [%o0+(8*7)] 
    stx %i0, [%o0+(8*8)] 
    stx %i1, [%o0+(8*9)] 
    stx %i2, [%o0+(8*10)] 
    stx %i3, [%o0+(8*11)] 
    stx %i4, [%o0+(8*12)] 
    stx %i5, [%o0+(8*13)] 
    stx %i6, [%o0+(8*14)] 
    stx %i7, [%o0+(8*15)]
    !! Save SP, FP, PC (return address) 
    stx %fp, [%o0+(8*16)]
    stx %sp, [%o0+(8*17)]
    add %o7, 8, %g1
    stx %g1, [%o0+(8*19)]       !! return address
    !! Fill return value slot with 1
    stx %g0, [%o0+(8*18)]       !! return value
    retl
    mov 0, %o0                  !! return 0 
    .size   FIsetjmp_Conservative, .-FIsetjmp_Conservative

    !! The non-Conservative forms do not save and restore the L and I registers
    !! The semantics are closer to the system setjmp() and longjmp(). 
    .align  64
    .globl  FIsetjmp
    .type   FIsetjmp, #function
  FIsetjmp:
    !! Save SP, FP, PC (return address) 
    stx %i7, [%o0+(8*15)]
    stx %fp, [%o0+(8*16)]
    stx %sp, [%o0+(8*17)]
    add %o7, 8, %g1
    stx %g1, [%o0+(8*19)]       !! return address
    !! Fill return value slot with 1
    stx %g0, [%o0+(8*18)]       !! return value
    retl
    mov 0, %o0                  !! return 0 
    .size   FIsetjmp, .-FIsetjmp

    .globl  FIlongjmp_Conservative
    .type   FIlongjmp_Conservative, #function
  FIlongjmp_Conservative:
    mov %o1, %g3                !! save return value
    movrz %g3,1,%g3             !! ensure return value not zero
    mov %o0, %g1                !! keep pointer to jmpbuf in G register
    !! ASSERT fp <= jmpbuf->fp
    !! Trim the frames as needed with RESTORE
    !! while jmpbuf->fp != fp : RESTORE ; 
    ldx [%g1+(8*16)], %g2
  1:cmp %g2, %fp
    be  %xcc, 2f
    nop
    ba 1b
    restore                     !! Trim one frame
  2:!! Restore the save L and I registers from the jmpbuf
    !! We're assuming that the jmpbuf stays in scope (>=sp) while trimming.
    !! If that were ever NOT the case we could alternatively restore the
    !! L and I registers 1st, copy the FP,SP,PC,RV into Gx registers, and
    !! then trim the stack with restore. 
    ldx [%g1+(8*0)] , %l0
    ldx [%g1+(8*1)] , %l1
    ldx [%g1+(8*2)] , %l2
    ldx [%g1+(8*3)] , %l3
    ldx [%g1+(8*4)] , %l4
    ldx [%g1+(8*5)] , %l5
    ldx [%g1+(8*6)] , %l6
    ldx [%g1+(8*7)] , %l7
    ldx [%g1+(8*8)] , %i0
    ldx [%g1+(8*9)] , %i1
    ldx [%g1+(8*10)] , %i2
    ldx [%g1+(8*11)] , %i3
    ldx [%g1+(8*12)] , %i4
    ldx [%g1+(8*13)] , %i5
    !! Recall that sp EQU o6 and fp EQU i6, so restoring i6 is probably redundant.  
    !! XXXX ldx [%g1+(8*14)] , %i6
    ldx [%g1+(8*15)] , %i7
    !! fp has already been restored via RESTORE, so we need to restore only SP and PC. 
    ldx [%g1+(8*17)], %sp
    ldx [%g1+(8*19)], %g2
    jmpl %g2+%g0, %g0
    mov %g3, %o0            
    .size   FIlongjmp_Conservative, .-FIlongjmp_Conservative

    .globl  FIlongjmp
    .type   FIlongjmp, #function
  FIlongjmp:
    mov %o1, %g3                !! save return value
    movrz %g3,1,%g3             !! ensure return value not zero
    mov %o0, %g1                !! keep pointer to jmpbuf in G register
    !! ASSERT fp <= jmpbuf->fp
    !! Trim the frames as needed with RESTORE
    !! while jmpbuf->fp != fp : RESTORE ; 
    ldx [%g1+(8*16)], %g2
  1:cmp %g2, %fp                !! In originating frame ?
    be  %xcc, 2f
    nop
    ba 1b
    restore                     !! Trim one frame
  2:nop
    !! We're back in the originating frame
    ldx [%g1+(8*15)], %i7
    !! fp has already been restored via RESTORE, so we need to restore only SP and PC. 
    !! Recall that sp EQU o6 and fp EQU i6, so restoring i6, above, is probably redundant.  
    ldx [%g1+(8*17)], %sp
    ldx [%g1+(8*19)], %g2
    jmpl %g2+%g0, %g0
    mov %g3, %o0            
    .size   FIlongjmp, .-FIlongjmp

