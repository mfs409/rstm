/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "adaptivity.hpp"

namespace stm
{
  /**
   *  Collection of all known algorithms
   */
  alg_t tm_info[TM_NAMES_MAX];

  /**
   * Use this function to register your TM algorithm implementation.  It
   * takes a bunch of function pointers and an identifier from the TM_NAMES
   * enum.
   */
  void registerTMAlg(int identifier,
                     void (*tm_begin)(scope_t*),
                     void (*tm_end)(),
                     void* (* TM_FASTCALL tm_read)(void**),
                     void (* TM_FASTCALL tm_write)(void**, void*),
                     scope_t* (*rollback)(TX*),
                     const char* (*tm_getalgname)(),
                     void* (*tm_alloc)(size_t),
                     void (*tm_free)(void*))
  {
      tm_info[identifier].tm_begin = tm_begin;
      tm_info[identifier].tm_end = tm_end;
      tm_info[identifier].tm_read = tm_read;
      tm_info[identifier].tm_write = tm_write;
      tm_info[identifier].rollback = rollback;
      tm_info[identifier].tm_getalgname = tm_getalgname;
      tm_info[identifier].tm_alloc = tm_alloc;
      tm_info[identifier].tm_free = tm_free;
  }

}
