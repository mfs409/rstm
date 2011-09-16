/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef OTM2STM_TYPEALIGNMENTS_HPP__
#define OTM2STM_TYPEALIGNMENTS_HPP__

#include "stm/config.h"

// This file abstracts away the fact that on some architectures, some
// accesses are guaranteed to be aligned.
//
// In practice, there's more at play here.  Byte accesses are always aligned,
// but that doesn't matter, because byte accesses are subword.  So in truth,
// it doesn't matter what we return for "byte".
//
// For everything else, the rules are remarkably simple.  On SPARC,
// everything is aligned.  On x86, nothing that Oracle's TM compiler supports
// is guaranteed to be aligned.

namespace
{

  // Abstracts some platform-specific alignment issues. Each arch directory
  // implements this class appropriately to tell the barrier code if a particular
  // type is guaranteed to be aligned on the platform.
  template <typename T>
  struct Aligned {
#if defined (STM_CPU_X86)
      enum { value = false };
#elif defined (STM_CPU_SPARC)
      enum { value = true };
#else
#error "Unrecognized CPU type.  Only x86 and SPARC are supported."
#endif
  };
}

#endif // OTM2STM_TYPEALIGNMENTS_HPP__
