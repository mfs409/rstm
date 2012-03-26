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
 * NOrec with HourglassBackoffCM
 */

#include "NOrec.hpp"
#include "cm.hpp"
#include "adaptivity.hpp"

using namespace stm;

/**
 * Instantiate rollback with the appropriate CM for this TM algorithm
 */
template scope_t* norec_generic::rollback_generic<HourglassBackoffCM>(TX*);

/**
 * Instantiate tm_begin with the appropriate CM for this TM algorithm
 */
template void norec_generic::tm_begin_generic<HourglassBackoffCM>(scope_t*);

/**
 * Instantiate tm_end with the appropriate CM for this TM algorithm
 */
template void norec_generic::tm_end_generic<HourglassBackoffCM>();

namespace norechb
{
  scope_t* rollback(TX* tx) __attribute__((weak, alias("_ZN13norec_generic16rollback_genericIN3stm18HourglassBackoffCMEEEPvPNS1_2TXE")));

  void tm_begin(scope_t *) __attribute__((weak, alias("_ZN13norec_generic16tm_begin_genericIN3stm18HourglassBackoffCMEEEvPv")));
  void tm_end() __attribute__((weak, alias("_ZN13norec_generic14tm_end_genericIN3stm18HourglassBackoffCMEEEvv")));
  TM_FASTCALL
  void* tm_read(void**) __attribute__((weak, alias("_ZN13norec_generic7tm_readEPPv")));
  TM_FASTCALL
  void tm_write(void**, void*) __attribute__((weak, alias("_ZN13norec_generic8tm_writeEPPvS0_")));
  void* tm_alloc(size_t) __attribute__((weak, alias("_ZN13norec_generic8tm_allocEj")));
  void tm_free(void*) __attribute__((weak, alias("_ZN13norec_generic7tm_freeEPv")));

  /**
   *  For querying to get the current algorithm name
   */
  const char* tm_getalgname() { return "NOrecHB"; }

}

/**
 * Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(NOrecHB, norechb);
REGISTER_TM_FOR_STANDALONE(norechb, 7);
