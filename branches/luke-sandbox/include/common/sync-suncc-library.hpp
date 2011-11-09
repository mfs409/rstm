/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

// Define our atomic interface entirely in terms of suncc's library.

#ifndef STM_COMMON_PLATFORM_SYNC_SUNCC_LIBRARY_HPP
#define STM_COMMON_PLATFORM_SYNC_SUNCC_LIBRARY_HPP

#include <atomic.h>

namespace stm
{
  namespace sync_suncc_library
  {
    /// Our partial specialization helper is parameterized based on the type
    /// (T), the number of bytes in the type (N), and just serves to dispatch
    /// to the right library atomic.
    template <typename T,
              size_t N = sizeof(T)>
    struct SYNC;

    /// The byte implementations.
    template <typename T>
    struct SYNC<T, 1>
    {
        static T swap(volatile T* address, T value) {
            return atomic_swap_8(address, value);
        }
    };

    /// The word (4-byte) implementation.
    template <typename T>
    struct SYNC<T, 4>
    {
        // Use the swap instruction here.
        static T swap(volatile T* address, T value)
        {
            return atomic_swap_32(address, value);
        }

        // We can cas a word-sized value with a single sparc cas
        static T cas(volatile T* ptr, T old, T _new)
        {
            return atomic_cas_32(ptr, old, _new);
        }

        static T fai(volatile T* ptr)
        {
            return atomic_inc_32_nv(address) - 1;
        }

        static T faa(volatile T* ptr, const int32_t a)
        {
            return atomic_add_32(ptr, a);
        }
    };

    /// The doubleword (8-byte) implementations.
    template <typename T>
    struct SYNC<T, 8>
    {
        static T swap(volatile T* address, T value)
        {
            return atomic_swap_64(address, value);
        }

        static T cas(volatile T* addr, T from, T to)
        {
            return atomic_cas_64(addr, from, to);
        }

        static T fai(volatile T* ptr)
        {
            return __sync_fetch_and_add(p, 1);
        }

        static T faa(volatile T* ptr, const int64_t a)
        {
            return atomic_add_64(ptr, a);
        }
    };

    /// The pointer versions.
    template <typename T>
    struct SYNC<T*, sizeof(T)>
    {
        static T* swap(volatile T** address, T* value)
        {
            return atomic_swap_ptr(address, value);
        }

        static T* cas(volatile T** addr, T* from, T* to)
        {
            return atomic_cas_ptr(addr, from, to);
        }

        static T* fai(volatile T** address)
        {
            return (T*)(atomic_inc_ulong_nv((volatile unsigned long*)p) - 1);
        }

        static T* faa(volatile T** address, unsigned long a)
        {
            return (T*)(atomic_add_long((bolatile unsigned long*)p, a));
        }
    };
  } // namespace stm::sync_suncc_library

  template <typename T>
  inline T
  sync_cas(volatile T* address, T from, T to)
  {
      return sync_suncc_library::SYNC<T>::cas(address, from, to);
  }

  template <typename T>
  inline bool
  sync_bcas(volatile T* address, T from, T to)
  {
      return sync_cas(address, from, to) == from;
  }

  /// Lock test and set is implemented directly
  template <typename T>
  inline T
  sync_tas(volatile T* address)
  {
      // second parameter is the bit to set
      return atomic_set_long_excl(address, 0);
  }

  template <typename T>
  inline T
  sync_swap(volatile T* addr, T val)
  {
      return sync_suncc_library::SYNC<T>::swap(addr, val);
  }

  template <typename T, typename S>
  inline T
  sync_faa(volatile T* address, S value)
  {
      return sync_suncc_library::SYNC<T>::faa(address, value);
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
}

#endif // STM_COMMON_PLATFORM_SYNC_SUNCC_LIBRARY_HPP
