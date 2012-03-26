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

#include "OrecLazy.hpp"
#include "cm.hpp"
#include "adaptivity.hpp"

using namespace stm;

/**
 * Instantiate rollback with the appropriate CM for this TM algorithm
 */
template scope_t* oreclazy_generic::rollback_generic<BackoffCM>(TX*);

/**
 * Instantiate tm_begin with the appropriate CM for this TM algorithm
 */
template void oreclazy_generic::tm_begin_generic<BackoffCM>(scope_t*);

/**
 * Instantiate tm_end with the appropriate CM for this TM algorithm
 */
template void oreclazy_generic::tm_end_generic<BackoffCM>();

namespace oreclazybackoff
{
  /**
   * Create aliases to the oreclazy_generic functions or instantiations that
   * we shall use for oreclazy
   */
  scope_t* rollback(TX* tx) __attribute__((weak, alias("_ZN16oreclazy_generic16rollback_genericIN3stm9BackoffCMEEEPvPNS1_2TXE")));

  void tm_begin(scope_t *) __attribute__((weak, alias("_ZN16oreclazy_generic16tm_begin_genericIN3stm9BackoffCMEEEvPv")));
  void tm_end() __attribute__((weak, alias("_ZN16oreclazy_generic14tm_end_genericIN3stm9BackoffCMEEEvv")));
  TM_FASTCALL
  void* tm_read(void**) __attribute__((weak, alias("_ZN16oreclazy_generic7tm_readEPPv")));
  TM_FASTCALL
  void tm_write(void**, void*) __attribute__((weak, alias("_ZN16oreclazy_generic8tm_writeEPPvS0_")));
  void* tm_alloc(size_t) __attribute__((weak, alias("_ZN16oreclazy_generic8tm_allocEj")));
  void tm_free(void*) __attribute__((weak, alias("_ZN16oreclazy_generic7tm_freeEPv")));

  /**
   *  For querying to get the current algorithm name
   */
  const char* tm_getalgname() { return "OrecLazyBackoff"; }

}
/**
 * Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(OrecLazyBackoff, oreclazybackoff);
REGISTER_TM_FOR_STANDALONE(oreclazybackoff, 15);
