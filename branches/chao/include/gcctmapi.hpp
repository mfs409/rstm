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
 * This API file defines how a benchmark should be built when we are using
 * the CGL (no instrumentation) interface.
 */

#ifndef GCCTMAPI_HPP__
#define GCCTMAPI_HPP__

#define TM_BEGIN(x)          __transaction_atomic {
#define TM_END()             }

#define TM_GET_ALGNAME()     "gcc-tm"

#define TM_READ(var)         var
#define TM_WRITE(var, val)   var = val

#define TM_THREAD_INIT()
#define TM_THREAD_SHUTDOWN()
#define TM_SYS_INIT()
#define TM_SYS_SHUTDOWN()
#define TM_ALLOC(s)          malloc(s)
#define TM_FREE(p)           free(p)
#define TM_BEGIN_FAST_INITIALIZATION()
#define TM_END_FAST_INITIALIZATION()
#define TM_CALLABLE          __attribute__((transaction_safe))
#define TM_WAIVER

#endif // GCCTMAPI_HPP__
