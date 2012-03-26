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
 * OrecLazyHB is the name for the oreclazy algorithm when instantiated with
 * HourglassBackoffCM.  Virtually all of the code is in the oreclazy.hpp
 * file, but we need to instantiate in order to use the "HourglassBackoffCM"
 * object, which employs both backoff and the "Hourglass" (from the "Toxic
 * Transactions" paper).
 */

#include "OrecLazy.hpp"
#include "cm.hpp"
#include "adaptivity.hpp"

namespace stm
{
  /**
   * Instantiate rollback with the appropriate CM for this TM algorithm
   */
  template scope_t* rollback_generic<HourglassBackoffCM>(TX*);
  scope_t* rollback(TX* tx) __attribute__((weak, alias("_ZN3stm16rollback_genericINS_18HourglassBackoffCMEEEPvPNS_2TXE")));

  /**
   * Instantiate tm_begin with the appropriate CM for this TM algorithm
   */
  template void tm_begin_generic<HourglassBackoffCM>(scope_t*);
  void tm_begin(scope_t *) __attribute__((weak, alias("_ZN3stm16tm_begin_genericINS_18HourglassBackoffCMEEEvPv")));

  /**
   * Instantiate tm_end with the appropriate CM for this TM algorithm
   */
  template void tm_end_generic<HourglassBackoffCM>();
  void tm_end() __attribute__((weak, alias("_ZN3stm14tm_end_genericINS_18HourglassBackoffCMEEEvv")));

  /**
   *  For querying to get the current algorithm name
   */
  const char* tm_getalgname() { return "OrecLazyHB"; }

  /**
   *  Register the TM for adaptivity
   */
  REGISTER_TM_FOR_ADAPTIVITY(OrecLazyHB);

}
