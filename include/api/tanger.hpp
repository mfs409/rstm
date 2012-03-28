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
 *  This file provides a mapping from our internal RSTM benchmark interface to
 *  Tanger's native API. Tanger's native API is being phased out in favor of
 *  the C++ STM draft API, but this is still useful for testing older versions
 *  of Tanger, particularly the Tanger released with the DTMC 1.0.0 release,
 *  which is the last time that Tanger and UIUC's LLVM DSA alias analysis
 *  targeted the same version of LLVM, llvm-2.6 (as of this writing).
 *
 *  See http://tm.inf.tu-dresden.de/ for more information about Tanger and the
 *  Tanger API.
 *
 *  This is _not_ providing the llvm-gcc-tanger interface, but rather provides
 *  the Tanger markers directly.
 */
#ifndef STM_API_TANGER_H
#define STM_API_TANGER_H

#include "alt-license/tanger-stm.h"     // tanger native API
#include "common/platform.hpp"          // nop()
#include "stm/lib_globals.hpp"          // get_algname()

#define  TM_CALLABLE
#define  TM_BEGIN(TYPE)                  { tanger_begin();
#define  TM_END                            tanger_commit(); }
#define  TM_WAIVER
#define  TM_GET_THREAD()
#define  TM_ARG
#define  TM_ARG_ALONE
#define  TM_PARAM
#define  TM_PARAM_ALONE
#define  TM_READ(x)                     (x)
#define  TM_WRITE(x, y)                 (x) = (y)
#define  TM_SYS_INIT()                  /* stm::sys_init(NULL) */
#define  TM_THREAD_INIT()               /* stm::thread_init() */
#define  TM_THREAD_SHUTDOWN()           /* stm::thread_shutdown() */
#define  TM_SYS_SHUTDOWN()              /* stm::sys_shutdown() */
#define  TM_ALLOC                       malloc
#define  TM_FREE                        free
#define  TM_SET_POLICY(P)               stm::set_policy(P)
#define  TM_GET_ALGNAME()               stm::get_algname()
#define  TM_BEGIN_FAST_INITIALIZATION   nop
#define  TM_END_FAST_INITIALIZATION     nop

#endif // STM_API_TANGER_H
