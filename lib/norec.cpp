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
 * NOrec with HyperAggressiveCM (no backoff)
 */

#include "norec.hpp"
#include "cm.hpp"

namespace stm
{
  /**
   * Instantiate rollback with the appropriate CM for this TM algorithm
   */
  template scope_t* rollback_generic<HyperAggressiveCM>(TX*);
  scope_t* rollback(TX* tx) __attribute__((weak, alias("_ZN3stm16rollback_genericINS_17HyperAggressiveCMEEEPvPNS_2TXE")));

  /**
   * Instantiate tm_begin with the appropriate CM for this TM algorithm
   */
  template void tm_begin_generic<HyperAggressiveCM>(scope_t*);
  void tm_begin(scope_t *) __attribute__((weak, alias("_ZN3stm16tm_begin_genericINS_17HyperAggressiveCMEEEvPv")));

  /**
   * Instantiate tm_end with the appropriate CM for this TM algorithm
   */
  template void tm_end_generic<HyperAggressiveCM>();
  void tm_end() __attribute__((weak, alias("_ZN3stm14tm_end_genericINS_17HyperAggressiveCMEEEvv")));

  /**
   *  For querying to get the current algorithm name
   */
  const char* tm_getalgname() { return "NOrec"; }

}
