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

namespace
{
  // Abstracts some platform-specific alignment issues. Each arch directory
  // implements this class appropriately to tell the barrier code if a particular
  // type is guaranteed to be aligned on the platform.
  template <typename T>
  struct Aligned {
      enum { value = false };
  };
}

#endif // OTM2STM_TYPEALIGNMENTS_HPP__
