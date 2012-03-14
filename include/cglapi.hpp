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

#ifndef CGLAPI_H__
#define CGLAPI_H__

#include <limits.h>

namespace stm
{
  void tm_begin();
  void tm_end();
  const char* tm_getalgname();
  void tm_thread_init();
  void tm_thread_shutdown();
  void tm_sys_init();
  void tm_sys_shutdown();
  void* tm_alloc(size_t s);
  void tm_free(void* p);
}


#define TM_BEGIN(x)          stm::tm_begin();
#define TM_END               stm::tm_end()
#define TM_GET_ALGNAME()     stm::tm_getalgname()
#define TM_READ(var)         var
#define TM_WRITE(var, val)   var = val
#define TM_THREAD_INIT()     stm::tm_thread_init()
#define TM_THREAD_SHUTDOWN() stm::tm_thread_shutdown()
#define TM_SYS_INIT()        stm::tm_sys_init()
#define TM_SYS_SHUTDOWN()    stm::tm_sys_shutdown()
#define TM_ALLOC(s)          stm::tm_alloc(s)
#define TM_FREE(p)           stm::tm_free(p)
#define TM_CALLABLE
#define TM_ARG
#define TM_PARAM
#define TM_ARG_ALONE
#define TM_BEGIN_FAST_INITIALIZATION()
#define TM_END_FAST_INITIALIZATION()
#define TM_WAIVER
#define TM_PARAM_ALONE

#endif // CGLAPI_H__
