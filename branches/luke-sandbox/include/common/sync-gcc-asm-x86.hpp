/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

/// This file contains implementations of the __sync family of builtin
/// functions for C++ compilers that don't support them, written for x86 and
/// x86_64 architectures using gcc inline asm.

#ifndef STM_COMMON_PLATFORM_SYNC_GCC_ASM_X86_HPP
#define STM_COMMON_PLATFORM_SYNC_GCC_ASM_X86_HPP

#include <cstddef>  // size_t
#include <stdint.h> // uintptr_t

/// We're going to do some basic metaprogramming to emulate the interface that
/// we expect from the __sync builtins, i.e., if there is a correct __sync for
/// the type, then we'll use it.
namespace stm
{
  namespace sync_gcc_asm_x86
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

    /// The byte implementations.
    template <typename T, size_t W>
    struct SYNC<T, W, 1>
    {
        static T swap(volatile T* address, T value)
        {
            asm volatile("lock xchgb %[value], %[address]"
                         : [value]   "+q" (value),
                           [address] "+m" (*address)
                         :
                         : "memory");
            return value;
        }
    };

    /// The word (4-byte) implementations, independent of the platform
    /// bitwidth.
    template <typename T, size_t W>
    struct SYNC<T, W, 4>
    {
        static T swap(volatile T* address, T value)
        {
            asm volatile("lock xchgl %[value], %[address]"
                         : [value]   "+r" (value),
                           [address] "+m" (*address)
                         :
                         : "memory");
            return value;
        }

        // We can cas a word-sized value with a single x86 lock cmpxchgl
        static T cas(volatile T* addr, T from, T to)
        {
            asm volatile("lock cmpxchgl %[to], %[addr]"
                         :        "+a" (from),
                           [addr] "+m" (*addr)
                         : [to]   "q"  (to)
                         : "cc", "memory");
            return from;
        }

        // We exploit the fact that xmpxchgl sets the Z flag with this bcas
        // specialization.
        static bool bcas(volatile T* addr, T from, T to)
        {
            bool result;
            asm volatile("lock cmpxchgl %[to], %[addr]\n\t"
                         "setz %[result]"
                         : [result] "=q" (result),
                           [addr]   "+m" (*addr),
                                    "+a"(from)
                         : [to]     "q"  (to)
                         : "cc", "memory");
            return result;
        }
    };

    /// The doubleword (8-byte) implementations, for 32-bit platforms.
    template <typename T>
    struct SYNC<T, 4, 8>
    {
        // implemented in terms of cas, because we don't have an 8 byte
        // xchg8b.
        static T swap(volatile T* address, T value)
        {
            // read memory, then update memory with value, making sure noone
            // wrote a new value---ABA is irrelevant. Can't use val cas
            // because I need to know if my memory write happened.
            T mem;
            do {
                mem = *address;
            } while (!bcas(address, mem, value))
            return mem;
        }

        // 64-bit CAS
        //
        // Our implementation depends on if we are compiling in a PIC or
        // non-PIC manner. PIC will not let us use %ebx in inline asm, so in
        // that case we need to save %ebx first, and then call the cmpxchg8b.
        //
        // * cmpxchg8b m64: Compare EDX:EAX with m64. If equal, set ZF and load
        //                  ECX:EBX into m64. Else, clear ZF and load m64 into
        //                  EDX:EAX.
        //
        // again, we exploit the Z-flag side effect here
        static bool bcas(volatile T* addr, T from, T to)
        {
            union {
                T from;
                uint32_t to[2];
            } cast = { to };

            bool result;
#if defined(__PIC__)
            asm volatile("xchgl %%ebx, %[to_low]\n\t"  // Save %ebx
                         "lock cmpxchg8b\t%[addr]\n\t" // Perform the exchange
                         "movl %[to_low], %%ebx\n\t"   // Restore %ebx
                         "setz %[result]"
                         :           "+A" (from),
                           [result]  "=q" (result),
                           [addr]    "+m" (*addr)
                         : [to_high] "c"  (cast.to[1]),
                           [to_low]  "g"  (cast.to[0])
                         : "cc", "memory");
#else
            asm volatile("lock cmpxchg8b %[addr]\n\t"
                         "setz %[result]"
                         :          "+A" (from),
                           [result] "=q" (result),
                           [addr]   "+m" (*addr)
                         :          "c"  (cast.to[1]),
                                    "b"  (cast.to[0])
                         : "cc", "memory");
#endif
            return result;
        }
    };

    /// The doubleword (8-byte) sync implementations, for 64-bit
    /// platforms.
    template <typename T>
    struct SYNC<T, 8, 8>
    {
        static T swap(volatile T* address, T value)
        {
            asm volatile("lock xchgq %[value], %[address]"
                         : [value]   "+r" (value),
                           [address] "+m" (*address)
                         :
                         : "memory");
            return value;
        }

        // We can cas a word-sized value with a single x86 lock cmpxchgq
        static T cas(volatile T* addr, T from, T to)
        {
            asm volatile("lock cmpxchgq %[to], %[addr]"
                         :        "+a" (from),
                           [addr] "+m" (*addr)
                         : [to]   "q"  (to)
                         : "cc", "memory");
            return from;
        }

        static bool bcas(volatile T* addr, T from, T to)
        {
            bool result;
            asm volatile("lock cmpxchgq %[to], %[addr]\n\t"
                         "setz %[result]"
                         : [result] "=q"  (result),
                           [addr]   "+m"  (*addr),
                                    "+a" (from)
                         : [to]     "q"   (to)
                         : "cc", "memory");
            return result;
        }
    };
  } // namespace stm::sync_gcc_asm_x86

  template <typename T>
  inline bool
  sync_bcas(volatile T* address, T from, T to)
  {
      return sync_gcc_asm_x86::SYNC<T>::bcas(address, from, to);
  }

  template <typename T>
  inline T
  sync_cas(volatile T* address, T from, T to)
  {
      return sync_gcc_asm_x86::SYNC<T>::cas(address, from, to);
  }

  // TAS implemented with swap.
  template <typename T>
  inline T
  sync_tas(volatile T* address)
  {
      return sync_gcc_asm_x86::SYNC<T>::swap(address, 1);
  }

  template <typename T>
  inline T
  sync_swap(volatile T* addr, T val)
  {
      return sync_gcc_asm_x86::SYNC<T>::swap(address, 1);
  }

  /// We implement fetch_and_add implemented in terms of bcas. We actually
  /// don't have a problem with the type of the value parameter, as long as the
  /// T + S operator returns a T, which it almost always will.
  template <typename T, typename S>
  inline T
  sync_faa(volatile T* address, S value)
  {
      T mem;
      do {
          mem = *address;
      } while (!sync_bcas(address, mem, mem + value));
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

#endif // STM_COMMON_PLATFORM_SYNC_GCC_ASM_X86_HPP
