/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

/// GCC's sparc builtins work pretty well for most things. Unfortunately, we
/// can't quite use them in all circumstances because the gcc-4.3.1 __sync
/// primitives sometimes cause odd compiler crashes. This file deal with that.

#ifndef STM_COMMON_PLATFORM_SYNC_GCC_ASM_SPARC_HPP
#define STM_COMMON_PLATFORM_SYNC_GCC_ASM_SPARC_HPP

#include <cstddef>  // size_t
#include <stdint.h> // uintptr_t

// We're going to do some basic metaprogramming to emulate the interface that
// we expect from the __sync builtins, i.e., if there is a correct __sync for
// the type, then we'll use it.

namespace stm
{
  namespace sync_gcc_asm_sparc
  {
    /// Our partial specialization helper is parameterized based on the type
    /// (T), the number of bytes in the type (N), and if this is a subword,
    /// word or double-word access (W). We assume that all addresses are
    /// aligned.
    ///
    /// T is necessary so that the caller doesn't need to perform any casts.
    ///
    ///   N is necessary because our implementation depends on the number of
    ///   bytes.
    ///
    ///   W allows us to deduce the platform without ifdefing anything.
    ///
    /// NB: We've only implemented partial templates for what we actually
    /// use. Extending this is straightforward (other than actually
    /// implementing the operations in terms of inline asm).
    template <typename T,
              size_t W = sizeof(void*),
              size_t N = sizeof(T)>
    struct SYNC;

    /// The word (4-byte) implementation for sparcv8 and sparcv9.
    template <typename T, size_t W>
    struct SYNC<T, W, 4>
    {
        // Use the swap instruction here.
        static T swap(volatile T* address, T value)
        {
            __asm__ volatile("swap [%2], %0;"
                             :"=r"(value)
                             :"0"(value), "r"(address)
                             :"memory");
            return value;
        }

        // We can cas a word-sized value with a single sparc cas
        static T cas(volatile T* ptr, T old, T _new)
        {
            __asm__ volatile("cas [%2], %3, %0"
                             : "=&r"(_new)
                             : "0"(_new), "r"(ptr), "r"(old)
                             : "memory");
            return _new;
        }
    };

    /// The doubleword (8-byte) implementations, for 32-bit platforms.
    template <typename T>
    struct SYNC<T, 4, 8>
    {
        static T cas(volatile T* addr, T from, T to)
        {
            // apparently the builtin works here.
            return __sync_val_compare_and_swap(addr, from, to);
        }
    };

    /// The doubleword (8-byte) sync implementations, for 64-bit
    /// platforms. Note that these contain 64-bit specific asm, which would
    /// cause a problem when assembling on a 32-bit architecture. The trick is
    /// that these won't be instantiated on 32-bit architectures and thus the
    /// C++ template rules say that they won't get compiled.
    template <typename T>
    struct SYNC<T, 8, 8>
    {
        // v9 instruction set says to implement in terms of cas
        static T swap(volatile T* address, T value) {
            T saw;
            do {
                T saw = *address;
            } while (SYNC<T, 8, 8>::cas(address, saw, value) != saw);
            return saw;
        }

        // We can cas a word-sized value with a single sparc casx
        static T cas(volatile T* addr, T from, T to)
        {
            __asm__ volatile("casx [%2], %3, %0"
                             : "=&r"(_new)
                             : "0"(_new), "r"(ptr), "r"(old)
                             : "memory");
            return _new;
        }
    };
  } // namespace stm::sync_gcc_asm_sparc

  template <typename T>
  inline T
  sync_cas(volatile T* address, T from, T to)
  {
      return sync_gcc_asm_sparc::SYNC<T>::cas(address, from, to);
  }

  template <typename T, typename S>
  inline bool
  sync_bcas(volatile T* address, S from, S to)
  {
      return sync_cas(address, from, to) == from;
  }

  template <typename T>
  inline T
  sync_swap(volatile T* addr, T val)
  {
      return sync_gcc_asm_sparc::SYNC<T>::swap(address, value);
  }

  template <typename T>
  inline T
  sync_tas(volatile T* address)
  {
      return sync_swap(address, 0);
  }

  template <typename T, typename S>
  inline T
  sync_faa(volatile T* address, S value)
  {
      T mem = *address;
      // NB: mem + value must be a T
      do {
          mem = *address;
      } while (sync_cas(address, mem, mem + value) != mem);
      return mem;
  }

  template <typename T>
  inline T
  sync_fai(volatile T* address)
  {
      return sync_faa(address, 1);
  }

  template <typename T>
  inline T
  sync_faand(volatile T* address, T mask)
  {
      T mem;
      do
      {
          mem = *address;
      } while (!sync_bcas(&bits[bucket], mem, mem & mask));
      return mem;
  }

  template <typename T>
  inline T
  sync_faor(volatile T* address, T mask)
  {
      T mem;
      do
      {
          mem = *address;
      } while (!sync_bcas(&bits[bucket], mem, mem | mask));
      return mem;
  }
} // namespace stm

#endif // STM_COMMON_PLATFORM_SYNC_GCC_ASM_SPARC_HPP
