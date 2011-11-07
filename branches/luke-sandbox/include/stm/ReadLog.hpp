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

#include <csignal>
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
      ReadLog(const unsigned long capacity);

      /// Override reset to also reset our cursor. This will forward to
      /// OrecList::reset internally.
      void reset();

      /// Orec-based sandboxed implementations can put off hashing until they
      /// need to valiate. This function scans through the [cursor_, m_size)
      /// sequence and treats all of the addresses there as plain addresses
      /// hashing them and updating their Read-log entry.
      ///
      /// This returns true if any modifications were made, and false if nothing
      /// needed to be done.
      bool doLazyHashes();

    private:
      /// The cursor keeps track of the next unhashed address in the read log,
      /// when we're using lazy hashing with a sandboxed orec TM.
      size_t cursor_;

      /// For debugging purposes, to verify that doLazyHashes is being used
      /// correctly. This is never touched for sandboxed STMs.
      volatile std::sig_atomic_t hashing_;

      /// Extends the expand functionality to only expand if the calling thread
      /// can be validated.
      void expand();
  };
}

#endif // STM_READLOG_HPP
