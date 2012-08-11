/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef BYTELOCKS_HPP__
#define BYTELOCKS_HPP__

#include <stdint.h>
#include "../include/abstract_cpu.hpp"
#include "MiniVector.hpp"
#include "Globals.hpp"

namespace stm
{
  /**
   *  TLRW-style algorithms don't use orecs, but instead use "byte locks".
   *  This is the type of a byte lock.  We have 32 bits for the lock, and
   *  then 60 bytes corresponding to 60 named threads.
   *
   *  NB: We don't support more than 60 threads in ByteLock-based algorithms.
   *      If you have more than that many threads, you should use adaptivity
   *      to switch to a different algorithm.
   */
  struct bytelock_t
  {
      volatile uint32_t      owner;      // no need for more than 32 bits
      volatile unsigned char reader[CACHELINE_BYTES - sizeof(uint32_t)];

      /**
       *  Setting the read byte is platform-specific, so we make it a method
       *  of the bytelock_t
       */
      void set_read_byte(uint32_t id);
  };

  extern bytelock_t    bytelocks[NUM_BYTELOCKS];         // set of bytelocks

  typedef MiniVector<bytelock_t*>  ByteLockList; // vector of bytelocks

  /**
   *  Map addresses to bytelock table entries
   */
  inline bytelock_t* get_bytelock(void* addr)
  {
      uintptr_t index = reinterpret_cast<uintptr_t>(addr);
      return &bytelocks[(index>>3) % NUM_BYTELOCKS];
  }

  /**
   *  Setting the read byte is platform-specific, so we are going to put it
   *  here to avoid lots of ifdefs in many code locations.  The issue is
   *  that we need this write to also be a WBR fence, and the cheapest WBR
   *  is platform-dependent
   */
  inline void bytelock_t::set_read_byte(uint32_t id)
  {
      // [mfs] DANGER!  This is no longer valid code, since ARM is a
      //       supported CPU.  We probably have bugs like this all over the
      //       place :(
#if defined(STM_CPU_SPARC)
      reader[id] = 1; WBR;
#else
      atomicswap8(&reader[id], 1u);
#endif
  }
}

#endif // BYTELOCKS_HPP__
