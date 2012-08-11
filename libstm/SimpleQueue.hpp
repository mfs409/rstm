/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef SIMPLEQUEUE_HPP__
#define SIMPLEQUEUE_HPP__

#include <stdint.h>

namespace stm
{

  /**
   * Linklist for Cohorts order handling.
   *
   * [mfs] what are these fields for?  Can we generalize this and move it to
   *       its own file?
   */
  struct cohorts_node_t
  {
      volatile uint32_t val;
      volatile uint32_t version;
      struct cohorts_node_t* next;
      /*** simple constructor */
      cohorts_node_t() : val(0), version(1), next(NULL)
      {}
  };

} // namespace stm

#endif // SIMPLEQUEUE_HPP__
