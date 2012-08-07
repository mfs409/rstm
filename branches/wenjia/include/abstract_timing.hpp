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
 *  This file provides an abstract interface to scheduling and timing
 *  functions.  It used to be more complex, but now that we do not have Win32
 *  support, everything is POSIX, and it's a little bit easier.  Note that we
 *  keep the abstractions, though, so that we can be sure that if we re-add
 *  Win32 support, it will be easy.
 *
 *  The key functions defined in this file are:
 *    sleep_ms()       - sleep for a number of milliseconds
 *    yield_cpu()      - offer to yield the CPU
 *    getElapsedTime() - access some sort of hi-res timer
 */

#ifndef ABSTRACT_TIMING_HPP__
#define ABSTRACT_TIMING_HPP__

#if defined(STM_OS_LINUX)

#  include <unistd.h>
#  include <stdio.h>
#  include <cstring>
#  include <pthread.h>
#  include <time.h>

  /**
   *  sleep_ms simply wraps the POSIX usleep call.  Note that usleep expects a
   *  number of microseconds, not milliseconds
   */
  inline void sleep_ms(uint32_t ms) { usleep(ms*1000); }

  /**
   *  Yield the CPU vis pthread_yield()
   */
  inline void yield_cpu() { pthread_yield(); }

  /**
   *  The Linux clock_gettime is reasonably fast, has good resolution, and is not
   *  affected by TurboBoost.  Using MONOTONIC_RAW also means that the timer is
   *  not subject to NTP adjustments, which is preferably since an adjustment in
   *  mid-experiment could produce some funky results.
   */
  inline uint64_t getElapsedTime()
  {
      struct timespec t;
      clock_gettime(CLOCK_REALTIME, &t);
      uint64_t tt = (((long long)t.tv_sec) * 1000000000L) + ((long long)t.tv_nsec);
      return tt;
  }

#endif // STM_OS_LINUX

#if defined(STM_OS_SOLARIS)

#  include <unistd.h>
#  include <sys/time.h>

  /**
   *  sleep_ms simply wraps the POSIX usleep call.  Note that usleep expects a
   *  number of microseconds, not milliseconds
   */
  inline void sleep_ms(uint32_t ms) { usleep(ms*1000); }

  /**
   *  Yield the CPU via yield()
   */
  inline void yield_cpu() { yield(); }

  /**
   *  We'll just use gethrtime() as our nanosecond timer
   */
  inline uint64_t getElapsedTime()
  {
      return gethrtime();
  }

#endif // STM_OS_SOLARIS

// [mfs] MacOS support has been dropped... we can keep this code for now, but
//       it is untested
#if defined(STM_OS_MACOS)

#  include <unistd.h>
#  include <mach/mach_time.h>
#  include <sched.h>

  /**
   *  sleep_ms simply wraps the POSIX usleep call.  Note that usleep expects a
   *  number of microseconds, not milliseconds
   */
  inline void sleep_ms(uint32_t ms) { usleep(ms*1000); }

  /**
   *  Yield the CPU via sched_yield()
   */
  inline void yield_cpu() { sched_yield(); }

  /**
   *  We'll use the MACH timer as our nanosecond timer
   *
   *  This code is based on code at
   *  http://developer.apple.com/qa/qa2004/qa1398.html
   */
  inline uint64_t getElapsedTime()
  {
      static mach_timebase_info_data_t sTimebaseInfo;
      if (sTimebaseInfo.denom == 0)
          (void)mach_timebase_info(&sTimebaseInfo);
      return mach_absolute_time() * sTimebaseInfo.numer / sTimebaseInfo.denom;
  }

#endif // STM_OS_MACOS

#endif // ABSTRACT_TIMING_HPP__
