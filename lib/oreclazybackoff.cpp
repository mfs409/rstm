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
 * OrecLazyBackoff is the name for the oreclazy algorithm when instantiated
 * backoff on abort.  Virtually all of the code is in the oreclazy.hpp file,
 * but we need to instantiate in order to use the "BackoffCM", which does
 * randomized exponential backoff.
 */

#include "oreclazy.hpp"
#include "cm.hpp"

namespace stm
{
  /**
   * Instantiate rollback with the appropriate CM for this TM algorithm
   */
  template scope_t* rollback_generic<BackoffCM>(TX*);
  scope_t* rollback(TX* tx) __attribute__((weak, alias("_ZN3stm16rollback_genericINS_9BackoffCMEEEPvPNS_2TXE")));

  /**
   * Instantiate tm_begin with the appropriate CM for this TM algorithm
   */
  template void tm_begin_generic<BackoffCM>(scope_t*);
  void tm_begin(scope_t *) __attribute__((weak, alias("_ZN3stm16tm_begin_genericINS_9BackoffCMEEEvPv")));

  /**
   * Instantiate tm_end with the appropriate CM for this TM algorithm
   */
  template void tm_end_generic<BackoffCM>();
  void tm_end() __attribute__((weak, alias("_ZN3stm14tm_end_genericINS_9BackoffCMEEEvv")));

  /**
   *  For querying to get the current algorithm name
   */
  const char* tm_getalgname() { return "OrecLazyBackoff"; }

}
