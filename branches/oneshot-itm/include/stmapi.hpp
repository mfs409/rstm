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
#include <cstdlib>

#include "../lib/tx.hpp" // TODO: temporary hack to check checkpoint behavior

#ifndef TM_FASTCALL
#if defined(STM_CPU_X86) && defined(STM_CC_GCC)
#    define TM_FASTCALL __attribute__((regparm(3)))
#else
#    define TM_FASTCALL
#endif
#endif

namespace stm
{
  uint32_t tm_begin(uint32_t);
  void tm_end();
  const char* tm_getalgname();
  void tm_thread_init();
  void tm_thread_shutdown();
  void tm_sys_init();
  void tm_sys_shutdown();
  void* tm_alloc(size_t s);
  void tm_free(void* p);
  TM_FASTCALL
  void* tm_read(void** addr);
  TM_FASTCALL
  void tm_write(void** addr, void* val);
}

#define TM_BEGIN(x) {                                  \
    setjmp(stm::Self->checkpoint);                     \
    stm::tm_begin(0x1);

#define TM_END()             stm::tm_end();     \
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
#define TM_BEGIN_FAST_INITIALIZATION() TM_BEGIN(atomic)
#define TM_END_FAST_INITIALIZATION()   TM_END()
#define TM_CALLABLE
#define TM_WAIVER

#endif // STMAPI_HPP__
