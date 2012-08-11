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
 *  Declare all constants that are needed throughout the RSTM implementation
 */

#ifndef CONSTANTS_HPP__
#define CONSTANTS_HPP__

#include <stdint.h>

namespace stm
{
  /**
   *  Many of our data structures benefit from having a cap on the number of
   *  threads.  Here we set that cap at 256.
   */
  const uint32_t MAX_THREADS = 256;

  /**
   * Specify the number of locks for various concurrency control mechanisms.
   * Note that the first batch should all be identical if you want an
   * apples-to-apples comparison
   */

  const uint32_t NUM_ORECS     = 1048576; // number of orecs
  const uint32_t NUM_BYTELOCKS = 1048576; // number of bytelocks
  const uint32_t NUM_BITLOCKS  = 1048576; // number of bitlocks
  const uint32_t NUM_RRECS     = 1048576; // number of rrecs

  const uint32_t NUM_NANORECS  = 1024;    // number of nanorecs

  const uint32_t SWISS_PHASE2  = 10;       // swisstm cm phase change thresh

}

#endif // CONSTANTS_HPP__
