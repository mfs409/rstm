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
 *  Define the basic types that we need in RSTM
 */

#ifndef BASICTYPES_HPP__
#define BASICTYPES_HPP__

#include "../include/abstract_cpu.hpp"

namespace stm
{
  /**
   *  Padded word-sized value for keeping a value in its own cache line
   */
  struct pad_word_t
  {
      volatile uintptr_t val;
      char pad[CACHELINE_BYTES-sizeof(uintptr_t)];
  };

  struct pad_word_t_int
  {
      volatile intptr_t val;
      char pad[CACHELINE_BYTES-sizeof(intptr_t)];
  };

#ifndef STM_CHECKPOINT_ASM
  /**
   *  A scope_t is an opaque type used by an API to unwind.
   */
  typedef void scope_t;
#endif
}

#endif // BASICTYPES_HPP__
