/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef TOXIC_HPP__
#define TOXIC_HPP__

#include <stdint.h>

namespace stm
{
  /**
   *  This is for counting consecutive aborts in a histogram.  We use it for
   *  measuring toxic transactions.  Note that there is special support for
   *  counting how many times an hourglass transaction commits or aborts.
   *
   *  [mfs] move to Toxic.hpp
   */
  struct toxic_histogram_t
  {
      /*** the highest number of consec aborts > 16 */
      uint32_t max;

      /*** how many hourglass commits occurred? */
      uint32_t hg_commits;

      /*** how many hourglass aborts occurred? */
      uint32_t hg_aborts;

      /*** histogram with 0-16 + overflow */
      uint32_t buckets[18];

      /*** on commit, update the appropriate bucket */
      void onCommit(uint32_t aborts);

      /*** simple printout */
      void dump();

      /*** on hourglass commit */
      void onHGCommit();

      /*** on hourglass abort() */
      void onHGAbort();

      /*** simple constructor */
      toxic_histogram_t() : max(0), hg_commits(0), hg_aborts(0)
      {
          for (int i = 0; i < 18; ++i)
              buckets[i] = 0;
      }
  };

  /*** on commit, update the appropriate bucket */
  inline void toxic_histogram_t::onCommit(uint32_t aborts)
  {
      if (aborts < 17) {
          buckets[aborts]++;
      }
      // overflow bucket: must also update the max value
      else {
          buckets[17]++;
          if (aborts > max)
              max = aborts;
      }
  }

  /*** on hourglass commit */
  inline void toxic_histogram_t::onHGCommit() { hg_commits++; }
  inline void toxic_histogram_t::onHGAbort() { hg_aborts++; }

  /**
   *  When STM_COUNTCONSEC_YES is not set, we don't do anything for these
   *  events
   */
  struct toxic_nop_t
  {
      void onCommit(uint32_t) { }
      void dump()             { }
      void onHGCommit()       { }
      void onHGAbort()        { }
  };

#ifdef STM_COUNTCONSEC_YES
  typedef toxic_histogram_t toxic_t;
#else
  typedef toxic_nop_t toxic_t;
#endif
}

#endif // TOXIC_HPP__
