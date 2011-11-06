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
 *  Your basic "useful but don't quite fit anywhere" utilities.
 */

#ifndef COMMON_UTILS_HPP
#define COMMON_UTILS_HPP

#include <cstdlib>

namespace stm
{
  /**
   *  We use malloc a couple of times here, and this makes it a bit easier.
   */
  template <typename T>
  inline T* typed_malloc(size_t N)
  {
      return static_cast<T*>(malloc(sizeof(T) * N));
  }

  /**
   *  Convince the compiler to tell us how many elements are in a statically
   *  sized array. This code appears in a lot of places on the web.
   */
  template <typename T, size_t N>
  inline size_t
  length_of(T(&)[N])
  {
      return N;
  }
}


#endif // COMMON_UTILS_HPP
