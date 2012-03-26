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
 * NOrec with HourglassCM
 */

#include "norec.hpp"
#include "cm.hpp"

namespace stm
{
  /**
   * Instantiate rollback with the appropriate CM for this TM algorithm
   */
  template scope_t* rollback_generic<HourglassCM>(TX*);
  scope_t* rollback(TX* tx) __attribute__((weak, alias("_ZN3stm16rollback_genericINS_11HourglassCMEEEPvPNS_2TXE")));

  /**
   * Instantiate tm_begin with the appropriate CM for this TM algorithm
   */
  template void tm_begin_generic<HourglassCM>(scope_t*);
  void tm_begin(scope_t *) __attribute__((weak, alias("_ZN3stm16tm_begin_genericINS_11HourglassCMEEEvPv")));

  /**
   * Instantiate tm_end with the appropriate CM for this TM algorithm
   */
  template void tm_end_generic<HourglassCM>();
  void tm_end() __attribute__((weak, alias("_ZN3stm14tm_end_genericINS_11HourglassCMEEEvv")));

  /**
   *  For querying to get the current algorithm name
   */
  const char* tm_getalgname() { return "NOrecHour"; }

}
