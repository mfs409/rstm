/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef RRECS_HPP__
#define RRECS_HPP__

#include "Globals.hpp"
#include "MiniVector.hpp"
#include "Constants.hpp"

namespace stm
{
  /**
   * a reader record (rrec) holds bits representing up to MAX_THREADS reader
   * transactions
   *
   *  NB: methods are implemented in algs.hpp, so that they are visible where
   *      needed, but not visible globally
   *
   *  [mfs] Move to rrec.hpp, and why not move methods there too?
   */
  struct rrec_t
  {
      /*** MAX_THREADS bits, to represent MAX_THREADS readers */
      static const uint32_t BUCKETS = MAX_THREADS / (8*sizeof(uintptr_t));
      static const uint32_t BITS    = 8*sizeof(uintptr_t);
      volatile uintptr_t    bits[BUCKETS];

      /*** set a bit */
      void setbit(unsigned slot);

      /*** test a bit */
      bool getbit(unsigned slot);

      /*** unset a bit */
      void unsetbit(unsigned slot);

      /*** combine test and set */
      bool setif(unsigned slot);

      /*** bitwise or */
      void operator |= (rrec_t& rhs);
  };

  extern rrec_t        rrecs[NUM_RRECS];              // set of rrecs

  typedef MiniVector<rrec_t*>      RRecList;     // vector of rrecs

  /**
   *  Map addresses to rrec table entries
   */
  inline rrec_t* get_rrec(void* addr)
  {
      uintptr_t index = reinterpret_cast<uintptr_t>(addr);
      return &rrecs[(index>>3)%NUM_RRECS];
  }

  /*** set a bit */
  inline void rrec_t::setbit(unsigned slot)
  {
      uint32_t bucket = slot / BITS;
      uintptr_t mask = 1lu<<(slot % BITS);
      uintptr_t oldval = bits[bucket];
      if (oldval & mask)
          return;
      while (true) {
          if (bcasptr(&bits[bucket], oldval, (oldval | mask)))
              return;
          oldval = bits[bucket];
      }
  }

  /*** test a bit */
  inline bool rrec_t::getbit(unsigned slot)
  {
      unsigned bucket = slot / BITS;
      uintptr_t mask = 1lu<<(slot % BITS);
      uintptr_t oldval = bits[bucket];
      return oldval & mask;
  }

  /*** unset a bit */
  inline void rrec_t::unsetbit(unsigned slot)
  {
      uint32_t bucket = slot / BITS;
      uintptr_t mask = 1lu<<(slot % BITS);
      uintptr_t unmask = ~mask;
      uintptr_t oldval = bits[bucket];
      if (!(oldval & mask))
          return;
      // NB:  this GCC-specific code
#if defined(STM_CPU_X86) && defined(STM_CC_GCC)
      __sync_fetch_and_and(&bits[bucket], unmask);
#else
      while (true) {
          if (bcasptr(&bits[bucket], oldval, (oldval & unmask)))
              return;
          oldval = bits[bucket];
      }
#endif
  }

  /*** combine test and set */
  inline bool rrec_t::setif(unsigned slot)
  {
      uint32_t bucket = slot / BITS;
      uintptr_t mask = 1lu<<(slot % BITS);
      uintptr_t oldval = bits[bucket];
      if (oldval & mask)
          return false;
      // NB: We don't have suncc fetch_and_or, so there is an ifdef here that
      //     falls back to a costly CAS-based atomic or
#if defined(STM_CPU_X86) && defined(STM_CC_GCC) /* little endian */
      __sync_fetch_and_or(&bits[bucket], mask);
      return true;
#else
      while (true) {
          if (bcasptr(&bits[bucket], oldval, oldval | mask))
              return true;
          oldval = bits[bucket];
      }
#endif
  }

  /*** bitwise or */
  inline void rrec_t::operator |= (rrec_t& rhs)
  {
      // NB: We could probably use SSE here, but since we've only got ~256
      //    bits, the savings would be minimal
      for (unsigned i = 0; i < BUCKETS; ++i)
          bits[i] |= rhs.bits[i];
  }
}

#endif // RRECS_HPP__
