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

namespace stm
{
  /**
   *  Many of our data structures benefit from having a cap on the number of
   *  threads.  Here we set that cap at 256.
   */
  static const unsigned MAX_THREADS = 256;
}

#endif // CONSTANTS_HPP__
