/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <stdio.h>
#include "Toxic.hpp"

namespace stm
{
  /*** simple printout */
  void toxic_histogram_t::dump()
  {
      printf("abort_histogram: ");
      for (int i = 0; i < 18; ++i)
          printf("%d, ", buckets[i]);
      printf("max = %d, hgc = %d, hga = %d\n", max, hg_commits, hg_aborts);
  }
}
