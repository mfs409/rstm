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
#include <cstdlib>
#include "libitm.h"

#if defined(STM_CPU_X86) && defined(STM_CC_GCC)
#    define TM_FASTCALL __attribute__((regparm(3)))
#else
#    define TM_FASTCALL
#endif

namespace stm
{
  void        tm_end();
  const char* tm_getalgname();
  void*       tm_alloc(size_t s);
  void        tm_free(void* p);
  void*       tm_read(void** addr) TM_FASTCALL;
  void        tm_write(void** addr, void* val) TM_FASTCALL;
}

// The RSTM library APIs have no "Cancel" construction.
#define TM_BEGIN(x)      { _ITM_beginTransaction(pr_instrumentedCode | pr_hasNoAbort);
#define TM_END()           stm::tm_end(); }
#define TM_GET_ALGNAME() stm::tm_getalgname()

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

#define TM_THREAD_INIT()
#define TM_THREAD_SHUTDOWN()
#define TM_SYS_INIT()
#define TM_SYS_SHUTDOWN()
#define TM_ALLOC(s)          stm::tm_alloc(s)
#define TM_FREE(p)           stm::tm_free(p)
#define TM_BEGIN_FAST_INITIALIZATION() TM_BEGIN(atomic)
#define TM_END_FAST_INITIALIZATION()   TM_END()
#define TM_CALLABLE
#define TM_WAIVER

#endif // STMAPI_HPP__
