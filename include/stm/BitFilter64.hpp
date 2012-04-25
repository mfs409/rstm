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
 *  This file implements a simple bit filter datatype, for 64 bit size
 */

#ifndef BITFILTER64_HPP__
#define BITFILTER64_HPP__

#include <stdint.h>

namespace stm
{
  /**
   *  This is a simple Bit vector class, with SSE2 optimizations
   */
  class BitFilter64
  {
      /*** CONSTS TO ALLOW ACCESS VIA WORDS/SSE REGISTERS */
      static const uint32_t WORD_SIZE   = 8 * sizeof(uintptr_t);
      static const uint32_t WORD_BLOCKS = 64 / WORD_SIZE;

      /**
       *  index this as an array of words or an array of vectors
       */
      union {
          uint32_t word_filter[WORD_BLOCKS];
      } TM_ALIGN(16);

      /*** simple hash function for now */
      ALWAYS_INLINE
      static uint32_t hash(const void* const key)
      {
          return (((uintptr_t)key) >> 3) % 64;
      }

    public:

      /*** constructor just clears the filter */
      BitFilter64() { clear(); }

      /*** simple bit set function */
      TM_INLINE
      void add(const void* const val) volatile
      {
          const uint32_t index  = hash(val);
          const uint32_t block  = index / WORD_SIZE;
          const uint32_t offset = index % WORD_SIZE;
          word_filter[block] |= (1u << offset);
      }

      /*** simple bit set function, with strong ordering guarantees */
      ALWAYS_INLINE
      void atomic_add(const void* const val) volatile
      {
          const uint32_t index  = hash(val);
          const uint32_t block  = index / WORD_SIZE;
          const uint32_t offset = index % WORD_SIZE;
#if defined(STM_CPU_X86)
          atomicswapptr(&word_filter[block],
                        word_filter[block] | (1u << offset));
#else
          word_filter[block] |= (1u << offset);
          WBR;
#endif
      }

      /*** simple lookup */
      ALWAYS_INLINE
      bool lookup(const void* const val) const volatile
      {
          const uint32_t index  = hash(val);
          const uint32_t block  = index / WORD_SIZE;
          const uint32_t offset = index % WORD_SIZE;

          return word_filter[block] & (1u << offset);
      }

      /*** simple union */
      TM_INLINE
      void unionwith(const BitFilter64& rhs)
      {
          word_filter[0] |= rhs.word_filter[0];
          word_filter[1] |= rhs.word_filter[1];
      }

      /*** a fast clearing function */
      TM_INLINE
      void clear() volatile
      {
          word_filter[0] = 0;
          word_filter[1] = 0;
      }

      /*** a bitwise copy method */
      TM_INLINE
      void fastcopy(const volatile BitFilter64* rhs) volatile
      {
          word_filter[0] = rhs->word_filter[0];
          word_filter[1] = rhs->word_filter[1];
      }

      /*** intersect two vectors */
      NOINLINE bool intersect(const BitFilter64* rhs) const volatile
      {
          uint32_t t1, t2;
          t1 = word_filter[0] & rhs->word_filter[0];
          t2 = word_filter[1] & rhs->word_filter[1];

          return (t1|t2);
      }
  }; // class stm::BitFilter64

}  // namespace stm

#endif // BITFILTER64_HPP__
