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
 *  The RSTM backends that use read orecs use this structure to log their
 *  orecs. Its a basic mini vector of orecs, with some added functionality for
 *  use by sandboxed TMs that want to be able to lazily hash addresses during
 *  validation.
 */

#ifndef STM_READLOG_HPP
#define STM_READLOG_HPP

#include <stm/config.h>
#include "stm/metadata.hpp"

namespace stm
{
  /**
   *  id_version_t uses the msb as the lock bit.  If the msb is zero, treat
   *  the word as a version number.  Otherwise, treat it as a struct with the
   *  lower 8 bits giving the ID of the lock-holding thread.
   */
  union id_version_t
  {
      struct
      {
          // ensure msb is lock bit regardless of platform
#if defined(STM_CPU_X86) /* little endian */
          uintptr_t id:(8*sizeof(uintptr_t))-1;
          uintptr_t lock:1;
#else /* big endian (probably SPARC) */
          uintptr_t lock:1;
          uintptr_t id:(8*sizeof(uintptr_t))-1;
#endif
      } fields;
      uintptr_t all; // read entire struct in a single load
  };

  /**
   * When we acquire an orec, we may ultimately need to reset it to its old
   * value (if we abort).  Saving the old value with the orec is an easy way to
   * support this need without having exta logging in the descriptor.
   */
  struct orec_t
  {
      volatile id_version_t v; // current version number or lockBit + ownerId
      volatile uintptr_t    p; // previous version number
  };


  typedef MiniVector<orec_t*> OrecList; // vector of orecs

  class ReadLog : public OrecList {
    public:
      ReadLog(const unsigned long capacity) : OrecList(capacity) {
      }
  };
}

#endif // STM_READLOG_HPP
