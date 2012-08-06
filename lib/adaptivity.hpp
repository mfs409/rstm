/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef RSTM_ADAPTIVITY_H
#define RSTM_ADAPTIVITY_H

#include <stdint.h>                     // uint32_t
#include "platform.hpp"                 // TM_FASTCALL
#include "libitm.h"                     // ITM_REGPARM, _ITM_*, etc.

namespace stm {
  struct TX;

  typedef uint32_t    (*tm_begin_t)(uint32_t, TX*, uint32_t) TM_FASTCALL;
  typedef void        (*tm_end_t)();
  typedef void*       (*tm_read_t)(void**) TM_FASTCALL;
  typedef void        (*tm_write_t)(void**, void*) TM_FASTCALL;
  typedef void*       (*tm_alloc_t)(size_t); //__attribute__((malloc))
  typedef void*       (*tm_calloc_t)(size_t, size_t); //__attribute__((malloc))
  typedef void        (*tm_free_t)(void*);
  typedef const char* (*tm_get_alg_name_t)();
  typedef void        (*tm_rollback_t)(TX*);
  typedef bool        (*tm_is_irrevocable_t)(TX*);
  typedef void (ITM_REGPARM *tm_become_irrevocable_t)(_ITM_transactionState);

  /**
   * Use this function to register your TM algorithm implementation.  It
   * takes a bunch of function pointers and an identifier from the TM_NAMES
   * enum.  This should be called by the initTM<> method.
   */
  void registerTMAlg(int, tm_begin_t, tm_end_t, tm_read_t, tm_write_t,
                          tm_rollback_t, tm_get_alg_name_t, tm_alloc_t,
                          tm_calloc_t, tm_free_t, tm_is_irrevocable_t,
                          tm_become_irrevocable_t);

  /**
   *  We don't want to have to declare an init function for each of the STM
   *  algorithms that exist, because there are very many of them.  Instead,
   *  we have a templated init function in namespace stm, and we instantiate
   *  it once per algorithm, in the algorithm's .cpp, using the TM_NAMES
   *  enum.  Then we can just call the templated functions from this code,
   *  and the linker will find the corresponding instantiation.
   */
  template <int I>
  void initTM();
}

/**
 * This hides the nastiness of registering algorithms with the adaptivity
 * mechanism. Include the autobuilt header that contains the ALG enum.
 */
#include <tmnames.autobuild.h>
#define REGISTER_TM_FOR_ADAPTIVITY(ALG)                                 \
    namespace stm {                                                     \
      template <> void initTM<ALG>() {                                  \
          registerTMAlg(ALG,                                            \
                        alg_tm_begin,                                   \
                        alg_tm_end,                                     \
                        alg_tm_read,                                    \
                        alg_tm_write,                                   \
                        alg_tm_rollback,                                \
                        alg_tm_getalgname,                              \
                        alg_tm_alloc,                                   \
                        alg_tm_calloc,                                  \
                        alg_tm_free,                                    \
                        alg_tm_is_irrevocable,                          \
                        alg_tm_become_irrevocable);                     \
      }                                                                 \
    }

#endif // RSTM_ADAPTIVITY_H
