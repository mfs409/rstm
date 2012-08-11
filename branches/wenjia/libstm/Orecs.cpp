/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "Orecs.hpp"

namespace stm
{
  /*** the set of orecs (locks) */
  orec_t orecs[NUM_ORECS] = {{{{0, 0}}, 0}};

  /*** the set of nanorecs */
  orec_t nanorecs[NUM_NANORECS] = {{{{0, 0}}, 0}};
}
