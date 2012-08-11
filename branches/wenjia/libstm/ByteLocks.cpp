/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "ByteLocks.hpp"

namespace stm
{
  /*** the table of bytelocks */
  bytelock_t bytelocks[NUM_BYTELOCKS] = {{0, {0}}};
}
