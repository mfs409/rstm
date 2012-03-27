// --------------------------------------------------------------------*- C -*-
// Copyright (C) 2012
// University of Rochester Department of Computer Science
//   and
// Lehigh University Department of Computer Science and Engineering
//
// License: Modified BSD
//          Please see the file LICENSE.RSTM for licensing information
// ----------------------------------------------------------------------------

#ifndef RSTM_LIBITM_H
#define RSTM_LIBITM_H

/// ---------------------------------------------------------------------------
///  Our definitions of the ITM ABI v 1.1 as described in
///  Intel-TM-ABI-1_1_20060506.pdf, available from
///  http://gcc.gnu.org/wiki/TransactionalMemory, combined with the gcc
///  modifications described in the gcc-libitm source release (libitm.pdf).
/// ---------------------------------------------------------------------------
#include <stdint.h>

#if defined(__i386__)
# define ITM_REGPARM __attribute__((regparm(2)))
#else
# define ITM_REGPARM
#endif

#define ITM_NORETURN __attribute__((noreturn))
#define GCC_ITM_PURE __attribute__((transaction_pure))

#ifdef __cplusplus
extern "C" {
#endif

    // flags argument to _ITM_beginTransaction
    typedef enum {
        pr_instrumentedCode     =      0x1,
        pr_uninstrumentedCode   =      0x2,
        pr_multiwayCode         = pr_instrumentedCode | pr_uninstrumentedCode,
        pr_hasNoVectorUpdat     =      0x4,
        pr_hasNoAbort           =      0x8,
        pr_hasNoFloatUpdate     =     0x10,
        pr_hasNoIrrevocable     =     0x20,
        pr_doesGoIrrevocable    =     0x40,
        pr_aWBarriersOmitted    =    0x100,
        pr_RaRBarriersOmitted   =    0x200,
        pr_undoLogCode          =    0x400,
        pr_preferUninstrumented =    0x800,
        pr_exceptionBlock       =   0x1000,
        pr_hasElse              =   0x2000,
        pr_readOnly             =   0x4000,
        pr_hasNoSimpleReads     = 0x400000
    } _ITM_codeProperties;

    // return value from _ITM_beginTransaction
    typedef enum {
        a_runInstrumentedCode       =  0x1,
        a_runUninstrumentedCode     =  0x2,
        a_saveLiveVariables         =  0x4,
        a_restoreLiveVariables      =  0x8,
        a_abortTransaction          = 0x10
    } _ITM_actions;


    // reason argument to _ITM_abortTransaction
    typedef enum {
        userAbort           =  0x1,
        userRetry           =  0x2,
        TMConflict          =  0x4,
        exceptionBlockAbort =  0x8,
        outerAbort          = 0x10
    } _ITM_abortReason;

    // argument to _ITM_changeTransactionMode
    typedef enum {
        modeSerialIrrevocable
    } _ITM_transactionState;

    // results from _ITM_inTransaction
    typedef enum {
        outsideTransaction = 0,
        inRetryableTransaction,
        inIrrevocableTransaction
    } _ITM_howExecuting;

    // This only appears in the _ITM_error function
    typedef struct {
        uint32_t reserved_1;
        uint32_t flags;
        uint32_t reserved_2;
        uint32_t reserved_3;
        const char *psource;
    } _ITM_srcLocation;

    typedef uint32_t _ITM_transactionId_t;

    typedef void (*_ITM_userUndoFunction)(void*);
    typedef _ITM_userUndoFunction _ITM_userCommitFunction;

#define _ITM_VERSION "0.9 (October 1, 2008)"
#define _ITM_VERSION_NO 90
#define _ITM_NoTransactionId 0

    // ------------------------------------------------------------------------
    // Official ABI functions
    // ------------------------------------------------------------------------
    extern int _ITM_versionCompatable(int)
        ITM_REGPARM;
    extern const char* _ITM_libraryVersion(void)
        ITM_REGPARM;
    extern void _ITM_error(const _ITM_srcLocation*, int)
        ITM_REGPARM ITM_NORETURN;
    extern _ITM_howExecuting _ITM_inTransaction(void)
        ITM_REGPARM;
    extern _ITM_transactionId_t _ITM_getTransactionId(void)
        ITM_REGPARM;
    extern uint32_t _ITM_beginTransaction(uint32_t, ...)
        ITM_REGPARM;
    extern void _ITM_abortTransaction(_ITM_abortReason)
         ITM_REGPARM ITM_NORETURN;
    extern void _ITM_commitTransaction(void)
         ITM_REGPARM;
    extern void _ITM_changeTransactionMode(_ITM_transactionState)
         ITM_REGPARM;
    extern void _ITM_addUserCommitAction(_ITM_userCommitFunction,
                                         _ITM_transactionId_t, void*)
         ITM_REGPARM;
    extern void _ITM_addUserUndoAction(_ITM_userUndoFunction, void*)
         ITM_REGPARM;
    extern void _ITM_dropReferences(void*, size_t)
         GCC_ITM_PURE;

    // ------------------------------------------------------------------------
    // gcc extensions
    // ------------------------------------------------------------------------
    extern void* _ITM_getTMCloneOrIrrevocable(void*)
         ITM_REGPARM;
    extern void  _ITM_registerTMCloneTable(void*, size_t);
    extern void  _ITM_deregisterTMCloneTable(void*);
    extern void* _ITM_cxa_allocate_exception(size_t);
    extern void  _ITM_cxa_throw(void*, void*, void*)
         ITM_REGPARM;
    extern void* _ITM_cxa_begin_catch(void*);
    extern void  _ITM_cxa_end_catch(void);
    extern void  _ITM_commitTransactionEH(void*)
         ITM_REGPARM;

    extern void* _ITM_malloc(size_t)
         __attribute__((__malloc__)) GCC_ITM_PURE;
    extern void* _ITM_calloc(size_t, size_t)
         __attribute__((__malloc__)) GCC_ITM_PURE;
    extern void  _ITM_free(void *)
         GCC_ITM_PURE;

    // ------------------------------------------------------------------------
    // data transfer functions
    // ------------------------------------------------------------------------
#define RSTM_LIBITM_DTFN(S, CC, R_TYPE, ...) extern R_TYPE S(__VA_ARGS__) CC;
#   include "libitm-dtfns.def"
#undef RSTM_LIBITM_DTFN

#ifdef __cplusplus
}
#endif

#endif // RSTM_LIBITM_H
