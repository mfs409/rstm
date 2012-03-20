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
 * the STM (no instrumentation) interface.
 */

#ifndef STMAPI_HPP__
#define STMAPI_HPP__

#include <limits.h>
#include <setjmp.h>

namespace stm
{
  void tm_begin(void*);
  void tm_end();
  const char* tm_getalgname();
  void tm_thread_init();
  void tm_thread_shutdown();
  void tm_sys_init();
  void tm_sys_shutdown();
  void* tm_alloc(size_t s);
  void tm_free(void* p);
  void* tm_read(void** addr);
  void tm_write(void** addr, void* val);
}

#define TM_BEGIN(x)                                    \
                             {                         \
                             jmp_buf _jmpbuf;          \
                             setjmp(_jmpbuf);          \
                             stm::tm_begin(&_jmpbuf);

#define TM_END()             stm::tm_end();   \
                             }

#define TM_GET_ALGNAME()     stm::tm_getalgname()

#include "library_inst.hpp"

/**
 *  Now we can make simple macros for reading and writing shared memory, by
 *  using templates to dispatch to the right code:
 */
namespace stm
{
  template <typename T>
  inline T stm_read(T* addr)
  {
      return DISPATCH<T, sizeof(T)>::read(addr);
  }

  template <typename T>
  inline void stm_write(T* addr, T val)
  {
      DISPATCH<T, sizeof(T)>::write(addr, val);
  }
} // namespace stm

#define TM_READ(var)         stm::stm_read(&var)
#define TM_WRITE(var, val)   stm::stm_write(&var, val)

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
#define TM_BEGIN_FAST_INITIALIZATION() TM_BEGIN(atomic)
#define TM_END_FAST_INITIALIZATION()   TM_END()
#define TM_WAIVER
#define TM_PARAM_ALONE

#endif // STMAPI_HPP__
