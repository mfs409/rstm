/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_TMABI_FPTR_H
#define RSTM_TMABI_FPTR_H

#include "platform.hpp"                 // TM_FASTCALL

namespace stm {
  /**
   *  All behaviors are reachable via function pointers.  This allows us to
   *  change on the fly. These are public because there is an api file
   *  (stmapi_fptr) that wants to call them directly. If stmapi_fptr ever goes
   *  away, we can drop this into a private namespace.
   *
   *  [ld] It would be nice to use the types defined in adaptivity.hpp for
   *       this, but we don't want to expose the autobuilt tmnames header. If
   *       we switch to constructor-based initialization we could use
   *       adaptivity.hpp.
   */
  struct TX;

  extern uint32_t    (*tm_begin_)(uint32_t, TX*) TM_FASTCALL;
  extern void        (*tm_end_)();
  extern void*       (*tm_read_)(void**) TM_FASTCALL;
  extern void        (*tm_write_)(void**, void*) TM_FASTCALL;
  extern void*       (*tm_alloc_)(size_t);
  extern void        (*tm_free_)(void*);
  extern const char* (*tm_getalgname_)();
  extern void        (*tm_rollback_)(TX*);
  extern bool        (*tm_is_irrevocable_)(TX*);
}

#endif // RSTM_TMABI_FPTR_H
