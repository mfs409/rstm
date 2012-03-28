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
                     tm_begin_t tm_begin,
                     tm_end_t tm_end,
                     tm_read_t tm_read,
                     tm_write_t tm_write,
                     rollback_t rollback,
                     tm_get_alg_name_t tm_getalgname,
                     tm_alloc_t tm_alloc,
                     tm_free_t tm_free)
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
