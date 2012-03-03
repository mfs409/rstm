/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef TMLINLINE_HPP__
#define TMLINLINE_HPP__

/**
 *  In order to support inlining of TML instrumentation, we must make some
 *  metadata and implementation code visible in this file.  It is provided
 *  below:
 */

#include "algs.hpp"

namespace stm
{
  /**
   *  TML requires this to be called after every read
   */
  inline void afterread_TML()
  {
      CFENCE;
      if (__builtin_expect(timestamp.val != Self.start_time, false))
          Self.tmabort();
  }

  /**
   *  TML requires this to be called before every write
   */
  inline void beforewrite_TML() {
      // acquire the lock, abort on failure
      if (!bcasptr(&timestamp.val, Self.start_time, Self.start_time + 1))
          Self.tmabort();
      ++Self.start_time;
      Self.tmlHasLock = true;
  }

} // namespace stm

#endif // TMLINLINE_HPP__
