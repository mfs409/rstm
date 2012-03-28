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
 *  Strictly speaking, any TM-supporting C++ compiler will not need this
 *  file.  However, if we want a program to be compilable with our library
 *  API, but also compilable with a TM C++ compiler, then we need to map the
 *  library calls to the calls the TM compiler expects.
 *
 *  NB: Right now this only works with the Intel compiler
 */

#ifndef STM_API_DTMC_HPP
#define STM_API_DTMC_HPP

#include "stm/lib_globals.hpp"

#define TM_CALLABLE
#define TM_BEGIN(TYPE)  __tm_atomic {
#define TM_END          }

#define TM_WAIVER

#define TM_GET_THREAD()
#define TM_ARG
#define TM_ARG_ALONE
#define TM_PARAM
#define TM_PARAM_ALONE

#define TM_READ(x) (x)
#define TM_WRITE(x, y) (x) = (y)

#define  TM_SYS_INIT()                   // _ITM_initializeProcess
#define  TM_THREAD_INIT()                // _ITM_initializeThread
#define  TM_THREAD_SHUTDOWN()            // _ITM_finalizeThread
#define  TM_SYS_SHUTDOWN()               // _ITM_finalizeProcess
#define  TM_ALLOC                      malloc
#define  TM_FREE                       free
#define  TM_SET_POLICY(P)
#define  TM_GET_ALGNAME()              stm::get_algname()
#define  TM_BEGIN_FAST_INITIALIZATION  nop
#define  TM_END_FAST_INITIALIZATION    nop

#endif // API_DTMC_HPP
