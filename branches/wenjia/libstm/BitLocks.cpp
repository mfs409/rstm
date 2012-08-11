/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "BitLocks.hpp"

namespace stm
{
  /*** the table of bitlocks */
  bitlock_t bitlocks[NUM_BITLOCKS] = {{0, {{0}}}};
}
