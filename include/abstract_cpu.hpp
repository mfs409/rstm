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
 *  This file provides an abstract interface to CPU-specific instructions and
 *  properties
 *
 *  The only declaration in this file is:
 *    CACHELINE_BYTES
 *
 *  In addition, we provide universal means of calling the following
 *  CPU-specific instructions:
 *    nop
 *    memory fences: LD/LD, LD/ST, ST/ST, and ST/LD
 *    cas: 32-bit, 64-bit, and word-sized
 *    bool cas: 32-bit, 64-bit, and word-sized
 *    tas
 *    swap: 8-bit, 32-bit, 64-bit, and word-sized
 *    fai: 32-bit, 64-bit, and word-sized
 *    faa: 32-bit, 64-bit, and word-sized
 *    mvx
 *    tick
 *    tickp
 */

#ifndef ABSTRACT_CPU_HPP__
#define ABSTRACT_CPU_HPP__

#include <stdint.h>
#include <limits.h>

// given a modern GCC (we shall soon require 4.7.0 and recommend 4.7.1 on
// ia32, amd64, sparc32, and sparc64; on ARM/Android it appears that the NDK
// is at least 4.4, and we'll probably soon update this requirement to
// 4.7-series as well), most of the cpu abstraction for atomic ops is
// achieved via the same set of __sync_XYZ primitives:
#if defined(STM_CC_GCC)
   // CAS instructions that return the old value
#  define cas32(p, o, n)      __sync_val_compare_and_swap(p, o, n)
#  define cas64(p, o, n)      __sync_val_compare_and_swap(p, o, n)
#  define casptr(p, o, n)     __sync_val_compare_and_swap(p, o, n)
   // CAS instructions that return a boolean
#  define bcas32(p, o, n)     __sync_bool_compare_and_swap(p, o, n)
#  define bcas64(p, o, n)     __sync_bool_compare_and_swap(p, o, n)
#  define bcasptr(p, o, n)    __sync_bool_compare_and_swap(p, o, n)
   // test and set
#  define tas(p)              __sync_lock_test_and_set(p, 1)
   // fetch and increment
#  define fai32(p)            __sync_fetch_and_add(p, 1)
#  define fai64(p)            __sync_fetch_and_add(p, 1)
#  define faiptr(p)           __sync_fetch_and_add(p, 1)
   // fetch and add (can take a negative)
#  define faa32(p, a)         __sync_fetch_and_add(p, a)
#  define faa64(p, a)         __sync_fetch_and_add(p, a)
#  define faaptr(p, a)        __sync_fetch_and_add(p, a)
#endif

// these defines are specific to ia32 and amd64 gcc targets
#if defined(STM_CPU_X86) && defined(STM_CC_GCC)
   // size of a cache line
#  define CACHELINE_BYTES     64
   // no-op
#  define nop()               __asm__ volatile("nop")
   // compiler and memory fences
#  define CFENCE              __asm__ volatile ("":::"memory")
#  define WBR                 __sync_synchronize()
   // NB: GCC implements test_and_set via xchg instructions, so this is safe
#  define atomicswap8(p, v)   __sync_lock_test_and_set(p, v)
#  define atomicswap32(p, v)  __sync_lock_test_and_set(p, v)
#  define atomicswap64(p, v)  __sync_lock_test_and_set(p, v)
#  define atomicswapptr(p, v) __sync_lock_test_and_set(p, v)
// atomic read of a 64-bit location depends on the BITS
#  if defined(STM_BITS_32)
    /**
     *  atomic 64-bit load on ia32: achieve via cast to double
     */
    inline void mvx(const volatile uint64_t* src, volatile uint64_t* dest)
    {
        const volatile double* srcd = (const volatile double*)src;
        volatile double* destd = (volatile double*)dest;
        *destd = *srcd;
    }
#  else
    /**
     *  atomic 64-bit load on amd64 is free, since 64-bit accesses are atomic
     */
    inline void mvx(const volatile uint64_t* src, volatile uint64_t* dest)
    {
        *dest = *src;
    }
#  endif
  // use rdtsc for high-precision tick counter
  inline uint64_t tick()
  {
      uint32_t tmp[2];
      asm volatile ("rdtsc" : "=a" (tmp[1]), "=d" (tmp[0]) : "c" (0x10) );
      return (((uint64_t)tmp[0]) << 32) | tmp[1];
  }
  // use rdtscp for high-precision tick counter with pipeline stall
  inline uint64_t tickp()
  {
      uint32_t tmp[2];
      asm volatile ("rdtscp" : "=a" (tmp[1]), "=d" (tmp[0]) : "c" (0x10) : "memory");
      return (((uint64_t)tmp[0]) << 32) | tmp[1];
  }
#endif // STM_CPU_X86 && STM_CC_GCC

// GCC armv7 is almost identical to GCC x86, but we split it out anyway,
// since a few things differ
#if defined(STM_CPU_ARMV7) && defined(STM_CC_GCC)
   // size of a cache line
#  define CACHELINE_BYTES     32
   // no-op
#  define nop()               __asm__ volatile("nop")
   // compiler and memory fences.
   // [mfs] these could be cleaner
#  define CFENCE              __asm__ volatile("dmb":::"memory")
#  define WBR                 __asm__ volatile("dmb":::"memory")
#  define WBW                 __asm__ volatile("dmb [st]":::"memory")
   // NB: GCC implements test_and_set via xchg instructions, so this is safe
   //
   // [mfs] I don't believe that the above statement is true for ARM... how
   //       can we use SWP instead?
#  define atomicswap8(p, v)   __sync_lock_test_and_set(p, v)
#  define atomicswap32(p, v)  __sync_lock_test_and_set(p, v)
#  define atomicswap64(p, v)  __sync_lock_test_and_set(p, v)
#  define atomicswapptr(p, v) __sync_lock_test_and_set(p, v)
  /**
   *  atomic 64-bit load is not available on armv7
   */
  inline void mvx(const volatile uint64_t* src, volatile uint64_t* dest)
  {
      assert(0 && "Atomic 64-bit load not supported on ARM");
  }
  /**
   *  We do not have tick() support for ARM yet
   *
   *  According to
   *  http://forums.arm.com/index.php?/topic/11702-way-to-get-cpu-tick-counter/,
   *  the right way to do it is probably "MRC p15, 0, r0, c15, c12, 1", but
   *  I haven't verified this yet.
   */
  inline uint64_t tick() { return 0; }
  /**
   *  [mfs] There is probably no equivalent to rdtscp on ARM... assert false?
   */
  inline uint64_t tickp() { return 0; }
#endif // STM_CPU_ARMV7 && STM_CC_GCC

// The overlap on GCC between sparc32 and sparc64 is small enough that we
// only do 32/64-bit specialization as needed within this block
#if defined(STM_CPU_SPARC) && defined(STM_CC_GCC)
   // size of a cache line
#  define CACHELINE_BYTES     64
   // no-op
#  define nop()               __asm__ volatile("nop")
   // compiler and memory fences
#  define CFENCE              __asm__ volatile ("":::"memory")
#  define WBR                 __sync_synchronize()
   // NB: SPARC swap instruction only is 32/64-bit... there is no atomicswap8
#ifdef STM_BITS_32
#define atomicswapptr(p, v)                                 \
    ({                                                      \
        __typeof((v)) v1 = v;                               \
        __typeof((p)) p1 = p;                               \
        __asm__ volatile("swap [%2], %0;"                   \
                     :"=r"(v1) :"0"(v1), "r"(p1):"memory"); \
        v1;                                                 \
    })
#define atomicswap32(p, v)                                  \
    ({                                                      \
        __typeof((v)) v1 = v;                               \
        __typeof((p)) p1 = p;                               \
        __asm__ volatile("swap [%2], %0;"                   \
                     :"=r"(v1) :"0"(v1), "r"(p1):"memory"); \
        v1;                                                 \
    })
#else
#define atomicswapptr(p, v)                     \
    ({                                          \
        __typeof((v)) tmp;                      \
        while (1) {                             \
            tmp = *(p);                         \
            if (bcasptr((p), tmp, (v))) break;  \
        }                                       \
        tmp;                                    \
    })
#endif
#if defined(STM_BITS_32)
  /**
   *  atomic 64-bit load on 32-bit sparc: use ldx/stx
   */
  inline void mvx(const volatile uint64_t* from, volatile uint64_t* to)
  {
      __asm__ volatile("ldx  [%0], %%o4;"
                       "stx  %%o4, [%1];"
                       :: "r"(from), "r"(to)
                       : "o4", "memory");
  }
#  else
  /**
   *  atomic 64-bit load on 64-bit sparc is free
   */
  inline void mvx(const volatile uint64_t* src, volatile uint64_t* dest)
  {
      *dest = *src;
  }
#  endif
   // read the tick counter
#  if defined(STM_CPU_SPARC) && defined(STM_BITS_32)
    /**
     *  32-bit SPARC: read the tick register into two 32-bit registers, then
     *  manually combine the result
     *
     *  This code is based on
     *    http://blogs.sun.com/d/entry/reading_the_tick_counter
     *  and
     *    http://sourceware.org/binutils/docs-2.20/as/Sparc_002dRegs.html
     */
    inline uint64_t tick()
    {
        uint32_t lo = 0, hi = 0;
        __asm__ volatile("rd   %%tick, %%o2;"
                         "srlx %%o2,   32, %[high];"
                         "sra  %%o2,   0,  %[low];"
                         : [high] "=r"(hi),
                           [low]  "=r"(lo)
                         :
                         : "%o2" );
        uint64_t ans = hi;
        ans = ans << 32;
        ans |= lo;
        return ans;
    }
#  elif defined(STM_BITS_64)
    /**
     *  64-bit SPARC: read the tick register into a regular (64-bit) register
     *
     *  This code is based on http://blogs.sun.com/d/entry/reading_the_tick_counter
     *  and http://sourceware.org/binutils/docs-2.20/as/Sparc_002dRegs.html
     */
    inline uint64_t tick()
    {
        uint64_t val;
        __asm__ volatile("rd %%tick, %[val]" : [val] "=r" (val) : :);
        return val;
    }
#  endif

  /**
   *  No equivalent to rdtscp that I'm aware of for SPARC, especially given our
   *  specialized use of the function, so we'll just define tickp the same as
   *  tick
   */
   inline uint64_t tickp() { return 0; }
#endif

// [mfs] I believe the rest of this file is not currently supported,
// but might be useful one day...
#if 0
/**
 *  Here is the declaration of atomic operations when we're using Sun Studio
 *  12.1.  These work for x86 and SPARC, at 32-bit or 64-bit
 */
#if (defined(STM_CPU_X86) || defined(STM_CPU_SPARC)) && defined(STM_CC_SUN)
#include <atomic.h>
#define CFENCE              __asm__ volatile("":::"memory")
#define WBR                 membar_enter()

#define cas32(p, o, n)      atomic_cas_32(p, (o), (n))
#define cas64(p, o, n)      atomic_cas_64(p, (o), (n))
#define casptr(p, o, n)     atomic_cas_ptr(p, (void*)(o), (void*)(n))
#define bcas32(p, o, n)     ({ o == cas32(p, (o), (n)); })
#define bcas64(p, o, n)     ({ o == cas64(p, (o), (n)); })
#define bcasptr(p, o, n)    ({ ((void*)o) == casptr(p, (o), (n)); })

#define tas(p)              atomic_set_long_excl((volatile unsigned long*)p, 0)

#define nop()               __asm__ volatile("nop")

#define atomicswap8(p, v)   atomic_swap_8(p, v)
#define atomicswap32(p, v)  atomic_swap_32(p, v)
#define atomicswap64(p, v)  atomic_swap_64(p, v)
#define atomicswapptr(p, v) atomic_swap_ptr(p, (void*)(v))

#define fai32(p)            (atomic_inc_32_nv(p)-1)
#define fai64(p)            __sync_fetch_and_add(p, 1)
#define faiptr(p)           (atomic_inc_ulong_nv((volatile unsigned long*)p)-1)
#define faa32(p, a)         atomic_add_32(p, a)
#define faa64(p, a)         atomic_add_64(p, a)
#define faaptr(p, a)        atomic_add_long((volatile unsigned long*)p, a)

//  NB: must shut off 'builtin_expect' support
#define __builtin_expect(a, b) a
#endif

#endif // 0

#endif // ABSTRACT_CPU_HPP__
