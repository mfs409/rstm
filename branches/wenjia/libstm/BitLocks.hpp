/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef BITLOCKS_HPP__
#define BITLOCKS_HPP__

#include "MiniVector.hpp"
#include "Globals.hpp"
#include "RRecs.hpp"

namespace stm
{
/**
 *  [mfs] These defines are for tuning backoff behavior, but they shouldn't
 *        be defines...
 */
#if defined(STM_CPU_SPARC)
#  define BITLOCK_READ_TIMEOUT        32
#  define BITLOCK_ACQUIRE_TIMEOUT     128
#  define BITLOCK_DRAIN_TIMEOUT       1024
#else // STM_CPU_X86
#  define BITLOCK_READ_TIMEOUT        32
#  define BITLOCK_ACQUIRE_TIMEOUT     128
#  define BITLOCK_DRAIN_TIMEOUT       256
#endif

  /**
   *  If we want to do an STM with RSTM-style visible readers, this lets us
   *  have an owner and a bunch of readers in a single struct, instead of via
   *  separate orec and rrec tables.  Note that these data structures do not
   *  have nice alignment
   *
   *  [mfs] Should we align these better?
   */
  struct bitlock_t
  {
      volatile uintptr_t owner;    // this is the single wrter
      rrec_t             readers;  // large bitmap for readers
  };

  extern bitlock_t     bitlocks[NUM_BITLOCKS];          // set of bitlocks

  typedef MiniVector<bitlock_t*>   BitLockList;  // vector of bitlocks

  /**
   *  Map addresses to bitlock table entries
   */
  inline bitlock_t* get_bitlock(void* addr)
  {
      uintptr_t index = reinterpret_cast<uintptr_t>(addr);
      return &bitlocks[(index>>3) % NUM_BITLOCKS];
  }
}

#endif // BITLOCKS_HPP__
