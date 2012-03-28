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

#include "NOrec.hpp"
#include "cm.hpp"
#include "adaptivity.hpp"

using namespace stm;

/**
 * Instantiate rollback with the appropriate CM for this TM algorithm
 */
template scope_t* norec_generic::rollback_generic<HourglassCM>(TX*);
/**
 * Instantiate tm_begin with the appropriate CM for this TM algorithm
 */
template void norec_generic::tm_begin_generic<HourglassCM>(scope_t*);
/**
 * Instantiate tm_end with the appropriate CM for this TM algorithm
 */
template void norec_generic::tm_end_generic<HourglassCM>();

namespace norechour
{
  scope_t* rollback(TX* tx) __attribute__((weak, alias("_ZN13norec_generic16rollback_genericIN3stm11HourglassCMEEEPvPNS1_2TXE")));

  void tm_begin(scope_t *) __attribute__((weak, alias("_ZN13norec_generic16tm_begin_genericIN3stm11HourglassCMEEEvPv")));
  void tm_end() __attribute__((weak, alias("_ZN13norec_generic14tm_end_genericIN3stm11HourglassCMEEEvv")));
  TM_FASTCALL
  void* tm_read(void**) __attribute__((weak, alias("_ZN13norec_generic7tm_readEPPv")));
  TM_FASTCALL
  void tm_write(void**, void*) __attribute__((weak, alias("_ZN13norec_generic8tm_writeEPPvS0_")));
  void* tm_alloc(size_t) __attribute__((weak, alias("_ZN13norec_generic8tm_allocEj")));
  void tm_free(void*) __attribute__((weak, alias("_ZN13norec_generic7tm_freeEPv")));

  /**
   *  For querying to get the current algorithm name
   */
  const char* tm_getalgname() { return "NOrecHour"; }

}

/**
 * Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(NOrecHour, norechour);
REGISTER_TM_FOR_STANDALONE(norechour, 9);
