/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_TMABI_HPP
#define RSTM_TMABI_HPP

#include <stdint.h>                     // uint32_t
#include "platform.hpp"                 // TM_FASTCALL

/**
 *  This defines the internal ABI for interacting with algorithm-specific
 *  behavior. Implementations that want to be compatible with adaptivity should
 *  instead use the tmabi-weak header and the registration macro.
 */
namespace stm {
  struct TX;

  uint32_t    tm_begin(uint32_t, TX*) TM_FASTCALL;
  void        tm_end();
  const char* tm_getalgname();
  void*       tm_alloc(size_t);
  void        tm_free(void*);
  void*       tm_read(void**) TM_FASTCALL;
  void        tm_write(void**, void*) TM_FASTCALL;
  void        tm_rollback(TX*);
  bool        tm_is_irrevocable(TX*);
}

#endif // RSTM_TMABI_HPP
