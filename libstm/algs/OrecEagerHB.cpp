/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "OrecEager.hpp"

namespace stm
{
  template <>
  void initTM<OrecEagerHB>()
  {
      // set the name
      stms[OrecEagerHB].name      = "OrecEagerHB";

      // set the pointers
      stms[OrecEagerHB].begin     = OrecEagerGenericBegin<HourglassBackoffCM>;
      stms[OrecEagerHB].commit    = OrecEagerGenericCommit<HourglassBackoffCM>;
      stms[OrecEagerHB].rollback  = OrecEagerGenericRollback<HourglassBackoffCM>;
      stms[OrecEagerHB].read      = OrecEagerGenericRead<HourglassBackoffCM>;
      stms[OrecEagerHB].write     = OrecEagerGenericWrite<HourglassBackoffCM>;
      stms[OrecEagerHB].irrevoc   = OrecEagerGenericIrrevoc<HourglassBackoffCM>;
      stms[OrecEagerHB].switcher  = OrecEagerGenericOnSwitchTo<HourglassBackoffCM>;
      stms[OrecEagerHB].privatization_safe = false;
  }
}

#ifdef STM_ONESHOT_ALG_OrecEagerHB
DECLARE_AS_ONESHOT_SIMPLE(OrecEagerGeneric<HourglassBackoffCM>)
#endif
