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
 *  This file hides differences that are based on compiler, CPU, and OS.  In
 *  particular, we define:
 *
 *    1) atomic operations (cas, swap, etc, atomic 64-bit load/store)
 *    2) access to the tick counter
 *    3) clean definitions of custom compiler constructs (__builtin_expect,
 *       alignment attributes, etc)
 *    4) scheduler syscalls (sleep, yield)
 *    5) a high-resolution timer
 */

#ifndef PLATFORM_HPP__
#define PLATFORM_HPP__

#include <stdint.h>
#include <climits>
#include "stm/config.h"

/**
 *  We set up a bunch of macros that we use to insulate the rest of the code
 *  from potentially platform-dependent behavior.
 *
 *  NB:  This is partially keyed off of __LP64__ which isn't universally defined
 *       for -m64 code, but it works on the platforms that we support.
 *
 *  NB2: We don't really support non-gcc compatible compilers, so there isn't
 *       any compiler logic in these ifdefs. If we begin to support the Windows
 *       platform these will need to be more complicated
 */

// We begin by hard-coding some macros that may become platform-dependent in
// the future.
#define CACHELINE_BYTES 64
#define NORETURN        __attribute__((noreturn))
#define NOINLINE        __attribute__((noinline))
#define ALWAYS_INLINE   __attribute__((always_inline))
#define USED            __attribute__((used))
#define REGPARM(N)      __attribute__((regparm(N)))

// Pick up the BITS define from the __LP64__ token.
#if defined(__LP64__)
#define STM_BITS_64
#else
#define STM_BITS_32
#endif

// GCC's fastcall attribute causes a warning on x86_64, so we don't use it
// there (it's not necessary in any case because of the native calling
// convention.
#if defined(__LP64__) && defined(STM_CPU_X86)
#define GCC_FASTCALL
#else
#define GCC_FASTCALL    __attribute__((fastcall))
#endif

// We rely on the configured parameters here (no cross platform building yet)
#if !defined(STM_CC_GCC) || !defined(STM_CPU_SPARC)
#define TM_INLINE       ALWAYS_INLINE
#else
#define TM_INLINE
#endif

#if defined(STM_CPU_X86) && !defined(STM_CC_SUN)
#define TM_FASTCALL     REGPARM(3)
#else
#define TM_FASTCALL
#endif

#define TM_ALIGN(N)     __attribute__((aligned(N)))

// Pick up the right synchronization primitives.
#if defined(STM_CPU_X86) && defined(__ICC)
#include "sync-gcc-asm-x86.hpp"
#elif defined(STM_CPU_SPARC) && defined(STM_CC_GCC)
#include "sync-gcc-asm-sparc.hpp"
#elif (defined(STM_CPU_X86) || defined(STM_CPU_SPARC)) && defined(STM_CC_SUN)
#include "sync-syncc-library.hpp"
#else
// We want to use the gcc builtins. The wrappers are stricter about the types
// that they accept than the underlying builtins---which isn't a bad thing but
// may require some casting at the call site.
namespace stm {
  template <typename T>
  inline bool
  sync_bcas(volatile T* addr, T f, T t)
  {
      return __sync_bool_compare_and_swap(addr, f, t);
  }

  template <typename T>
  inline T
  sync_cas(volatile T* addr, T f, T t)
  {
      return __sync_val_compare_and_swap(addr, f, t);
  }

  template <typename T>
  inline T
  sync_tas(volatile T* addr)
  {
      return __sync_lock_test_and_set(addr, 1);
  }

  template <typename T>
  inline T
  sync_swap(volatile T* addr, T val)
  {
      return __sync_lock_test_and_set(addr, val);
  }

  template <typename T>
  inline T
  sync_fai(volatile T* addr)
  {
      return __sync_fetch_and_add(addr, 1);
  }

  // FAA is a bit different from the other functions in that there are two
  // types involved, one for the memory location type, and the other for the
  template <typename T, typename S>
  inline T
  sync_faa(volatile T* addr, S val)
  {
      return __sync_fetch_and_add(addr, val);
  }

  template <typename T>
  inline T
  sync_faand(volatile T* addr, T mask)
  {
      return __sync_fetch_and_and(addr, mask);
  }

  template <typename T>
  inline T
  sync_faor(volatile T* addr, T mask)
  {
      return __sync_fetch_and_or(addr, mask);
  }
}
#endif

#define CFENCE __asm__ volatile("":::"memory")
#define nop()  __asm__ volatile("nop")

// Some architecture-dependent macros for synchronization
#if defined(STM_CC_GCC)
#define WBR    __sync_synchronize()
#elif defined(STM_CC_SUN)
#define WBR    membar_enter()
#define __builtin_expect(a, b) a
#endif

// Now we must deal with the ability to load/store 64-bit values safely.  In
// 32-bit mode, this is potentially a problem, so we handle 64-bit atomic
// load/store via the mvx() function.  mvx() depends on the bit level and the
// CPU.
#if defined(STM_BITS_64)
// 64-bit code is easy... 64-bit accesses are atomic
inline void
mvx(const volatile uint64_t* src, volatile uint64_t* dest)
{
    *dest = *src;
}
#endif

#if defined(STM_BITS_32) && defined(STM_CPU_X86)
// 32-bit on x86... cast to double
inline void
mvx(const volatile uint64_t* src, volatile uint64_t* dest)
{
    const volatile double* srcd = (const volatile double*)src;
    volatile double* destd = (volatile double*)dest;
    *destd = *srcd;
}
#endif

#if defined(STM_BITS_32) && defined(STM_CPU_SPARC)
// 32-bit on SPARC... use ldx/stx
inline void
mvx(const volatile uint64_t* from, volatile uint64_t* to)
{
    __asm__ volatile("ldx  [%0], %%o4;"
                     "stx  %%o4, [%1];"
                     :: "r"(from), "r"(to)
                     : "o4", "memory");
}
#endif

// The next task for this file is to establish access to a high-resolution CPU
// timer.
#if defined(STM_CPU_X86)
inline uint64_t
tick()
{
    // On x86, we use the rdtsc instruction
    uint32_t tmp[2];
    __asm__ ("rdtsc" : "=a" (tmp[1]), "=d" (tmp[0]) : "c" (0x10) );
    return (((uint64_t)tmp[0]) << 32) | tmp[1];
}
#endif

#if defined(STM_CPU_SPARC)
#if defined(STM_BITS_64)
/**
 *  64-bit SPARC: read the tick register into a regular (64-bit) register
 *
 *  This code is based on http://blogs.sun.com/d/entry/reading_the_tick_counter and
 *  http://sourceware.org/binutils/docs-2.20/as/Sparc_002dRegs.html
 */
inline uint64_t
tick()
{
    uint64_t val;
    __asm__ volatile("rd %%tick, %[val]" : [val] "=r" (val) : :);
    return val;
}
#else
/**
 *  32-bit SPARC: read the tick register into two 32-bit registers, then
 *  manually combine the result
 *
 *  This code is based on
 *    http://blogs.sun.com/d/entry/reading_the_tick_counter
 *  and
 *    http://sourceware.org/binutils/docs-2.20/as/Sparc_002dRegs.html
 */
inline uint64_t
tick()
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
#endif
#endif

// Set up a millisecond timer. This used to be complicated by windows support,
// but all of our current architectures implement usleep.
#include <unistd.h>

inline void
sleep_ms(uint32_t ms)
{
    usleep(ms*1000);
}

// Now we present a clock that operates in nanoseconds, instead of in ticks,
// and a function for yielding the CPU.  This code also depends on the OS.
#if defined(STM_OS_LINUX)
#include <stdio.h>
#include <cstring>
#include <assert.h>
#include <pthread.h>
#include <time.h>

inline void
yield_cpu()
{
    pthread_yield();
}

inline uint64_t
getElapsedTime()
{
    // The Linux clock_gettime is reasonably fast, has good resolution, and is
    // not affected by TurboBoost.  Using MONOTONIC_RAW also means that the
    // timer is not subject to NTP adjustments, which is preferably since an
    // adjustment in mid-experiment could produce some funky results.
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);

    uint64_t tt = (((long long)t.tv_sec) * 1000000000L) + ((long long)t.tv_nsec);
    return tt;
}

#endif // STM_OS_LINUX

#if defined(STM_OS_SOLARIS)
#include <sys/time.h>

inline void
yield_cpu() {
    yield();
}

inline uint64_t
getElapsedTime()
{
    // We'll just use gethrtime() as our nanosecond timer
    return gethrtime();
}

#endif // STM_OS_SOLARIS

#if defined(STM_OS_MACOS)
#include <mach/mach_time.h>
#include <sched.h>

inline void
yield_cpu()
{
    sched_yield();
}

inline uint64_t
getElapsedTime()
{
    // We'll use the MACH timer as our nanosecond timer. Based on
    // http://developer.apple.com/qa/qa2004/qa1398.html.
    static mach_timebase_info_data_t sTimebaseInfo;
    if (sTimebaseInfo.denom == 0)
        (void)mach_timebase_info(&sTimebaseInfo);
    return mach_absolute_time() * sTimebaseInfo.numer / sTimebaseInfo.denom;
}

#endif // STM_OS_MACOS

#endif // PLATFORM_HPP__
