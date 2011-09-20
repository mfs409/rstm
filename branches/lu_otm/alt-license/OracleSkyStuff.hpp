/*
 * Copyright (c) 2009-2010, Oracle and/or its affiliates.
 *
 * All rights reserved.
 *
 * Sun Microsystems, Inc. has intellectual property rights relating to
 * technology embodied in the product that is described in this
 * document. In particular, and without limitation, these intellectual
 * property rights may include one or more of the U.S. patents listed
 * at http://www.sun.com/patents and one or more additional patents or
 * pending patent applications in the U.S. and in other countries.
 *
 * U.S. Government Rights - Commercial software. Government users are
 * subject to the Sun Microsystems, Inc. standard license agreement
 * and applicable provisions of the FAR and its supplements.
 *
 * Use is subject to license terms. Sun, Sun Microsystems and the Sun
 * logo are trademarks or registered trademarks of Sun Microsystems,
 * Inc. in the U.S. and other countries. All SPARC trademarks are used
 * under license and are trademarks or registered trademarks of SPARC
 * International, Inc. in the U.S. and other countries.
 *
 * ----------------------------------------------------------------------
 *
 * This file is part of the Hybrid Transactional Memory Library
 * (SkySTM Library) developed and maintained by Yossi Lev, Virendra
 * Marathe and Dan Nussbaum of the Scalable Synchronization Research
 * Group at Sun Microsystems Laboratories
 * (http://research.sun.com/scalable/).
 *
 * Please send email to skystm-feedback@sun.com with feedback,
 * questions, or to request future announcements about SkySTM.
 *
 * ----------------------------------------------------------------------
 *
 * The SkySTM Library is available for use and modification under the
 * terms of version 2 of the GNU General Public License.  The GNU
 * General Public License is contained in the file
 * $(SKSTMLIB)/SOFTWARELICENSE.txt.
 *
 * The SkySTM Library can be redistributed and/or modified under the
 * terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * In addition, we ask that you adhere to the following requests if
 * you use this software:
 *
 *    If your use of this software contributes to a published paper,
 *    we request that you (1) cite our summary paper that appears on
 *    our website
 *    (http://research.sun.com/scalable/pubs/TRANSACT2009-ScalableSTMAnatomy.pdf)
 *    and (2) e-mail a citation for your published paper to
 *    skystm-feedback@sun.com.
 *
 *    If you redistribute derivatives of this software, we request
 *    that you notify us and either (1) ask people to register with us
 *    at skystm-feedback@sun.com or (2) collect registration
 *    information and periodically send it to us.
 *
 * The SkySTM Library is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY, NON-INFRINGEMENT or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 */

#ifndef ORACLESKYSTUFF_HPP__
#define ORACLESKYSTUFF_HPP__

#include <stdio.h>
#include <stddef.h>
#include <thread.h>
#include <assert.h>
#include <sys/resource.h>
#include <stm/config.h>

/**
 *  In order to interface with the Oracle Transactional Compiler, we require
 *  a shim library that matches the ABI that the compiler expects.  The
 *  following definitions are taken directly from the Oracle SkySTM code
 *  TypesAndDefs.h file.
 */
extern "C"
{
    //
    // Dummy structs for opaque {Rd,Wr}Handle types.
    //
    typedef struct {
        int dummy;
    } RdHandle;
    typedef struct {
        int dummy;
    } WrHandle;

    //
    // Basic Types.
    //
    typedef char               INT8;
    typedef short              INT16;
    typedef int                INT32;
    typedef long long          INT64;
    typedef unsigned char      UINT8;
    typedef unsigned short     UINT16;
    typedef unsigned int       UINT32;
    typedef unsigned long long UINT64;
    typedef UINT32             BOOL;

    //
    // Three-valued return status for Commit().
    //
    typedef enum {
        AbortedNoRetry   = -1,         // This means that SelfAbort() was called.
        AbortedRetry     =  0,
        CommittedNoRetry =  1
    } CommitStatus;
}

/**
 *  The following headers define the public "C" API to the SkySTM library.
 *  Any substitute for the SkySTM library (such as our shim) needs to
 *  implement these.  We'll put the headers directly into this file, so that
 *  there is no doubt that this interface belongs to Oracle.  The original
 *  SkySTM file from which these were taken is CWrapper.h.
 */
extern "C"
{
    void* STM_GetMyTransId();

    BOOL  STM_BeginTransaction(void* theTransId);
    BOOL  STM_ValidateTransaction(void* theTransId);
    CommitStatus  STM_CommitTransaction(void* theTransId);
    void  STM_SelfAbortTransaction(void* theTransId);
    BOOL  STM_CurrentlyUsingDecoratedPath(void* theTransId);

    RdHandle* STM_AcquireReadPermission
    (void* theTransId, void* theAddr, BOOL theValid);

    WrHandle* STM_AcquireWritePermission
    (void* theTransId, void* theAddr, BOOL theValid);
    WrHandle* STM_AcquireReadWritePermission
    (void* theTransId, void* theAddr, BOOL theValid);
    UINT8  STM_TranRead8
    (void* theTransId, RdHandle* theRdHandle, UINT8 * theAddr, BOOL theValid);
    UINT16 STM_TranRead16
    (void* theTransId, RdHandle* theRdHandle, UINT16* theAddr, BOOL theValid);
    UINT32 STM_TranRead32
    (void* theTransId, RdHandle* theRdHandle, UINT32* theAddr, BOOL theValid);
    UINT64 STM_TranRead64
    (void* theTransId, RdHandle* theRdHandle, UINT64* theAddr, BOOL theValid);
    float  STM_TranReadFloat32
    (void* theTransId, RdHandle* theRdHandle, float * theAddr, BOOL theValid);
    double STM_TranReadFloat64

    (void* theTransId, RdHandle* theRdHandle, double* theAddr, BOOL theValid);
    BOOL  STM_TranWrite8
    (void* theTransId, WrHandle* theWrHandle, UINT8 * theAddr,  UINT8 theVal, BOOL theValid);
    BOOL  STM_TranWrite16
    (void* theTransId, WrHandle* theWrHandle, UINT16* theAddr, UINT16 theVal, BOOL theValid);
    BOOL  STM_TranWrite32
    (void* theTransId, WrHandle* theWrHandle, UINT32* theAddr, UINT32 theVal, BOOL theValid);
    BOOL  STM_TranWrite64
    (void* theTransId, WrHandle* theWrHandle, UINT64* theAddr, UINT64 theVal, BOOL theValid);
    BOOL  STM_TranWriteFloat32
    (void* theTransId, WrHandle* theWrHandle, float * theAddr,  float theVal, BOOL theValid);
    BOOL  STM_TranWriteFloat64
    (void* theTransId, WrHandle* theWrHandle, double* theAddr, double theVal, BOOL theValid);

    void* STM_TranMalloc(void* theTransId, size_t theSize);
    void* STM_TranCalloc(void* theTransId, size_t theNElem, size_t theSize);
    void  STM_TranMFree(void* theTransId, void *theMemBlock);
    void* STM_TranMemAlign(void* theTransId, size_t theAlignment, size_t theSize);
    void* STM_TranValloc(void* theTransId, size_t theSize);
    void  STM_TranMemCpy(void* theTransId, void* theFromAddr, void* theToAddr,
                         unsigned long theSizeInBytes, UINT32 theAlignment);
}

/**
 *  This code is derived from the SkySTMTransObjMgr.cpp
 *  SkySTMTransObjMgr::AllocAndInitTran method.  It's purpose is to find the
 *  upper and lower stack bounds for the caller thread, which may be either
 *  the main thread or a pthread.
 */

#ifndef RSTM_INLINE_HEADER
//
// Allocate transaction, picking a slot at random in TransTable and
// searching linearly from there until an unallocated slot is found.
//
// Once a free slot is found, allocate and initialize a transaction
// for that slot (if necessary), initialize its stack-limit members
// and return it.
//
inline void getStackInfo(void*& lo, void*& hi)
{
    // Update transaction object with thread stack information.
    //
    stack_t ss;
    int retval = thr_stksegment(&ss);
    assert(retval == 0);

    // thr_stksegment() has problems when run on the main
    // thread, so we do the best we can.
    //
    if (thr_main()) {
        struct rlimit limits;

        retval = getrlimit(RLIMIT_STACK, &limits);
        assert(retval == 0);

        size_t adjusted_size = limits.rlim_cur;
        if ((ssize_t)adjusted_size < 0) {
            // Compensate for ridiculous stack size.
            // limits.rlim_cur can be negative (when stack size is
            // unlimited).  In that case, set the limit to 4G
            // (256M in 32bit mode).
            //
#if       !defined(STM_BITS_32)
            adjusted_size = 0x100000000;
#else  // MMARCH_SPARC_32BIT
            adjusted_size = 0x10000000;
#endif
        }
        if (adjusted_size > (size_t)ss.ss_sp) {
            // Make sure size doesn't allow the stack to wrap the
            // address space.
            //
            adjusted_size = (size_t)ss.ss_sp;
        }
        ss.ss_size = adjusted_size;
    }

    hi = ss.ss_sp;
    lo = (void*)((size_t)hi - ss.ss_size);

    // printf("Stack Information: LOW=0x%p, HIGH=0x%p\n", lo, hi);
}
#endif // RSTM_INLINE_HEADER

#endif // ORACLESKYSTUFF_HPP__
