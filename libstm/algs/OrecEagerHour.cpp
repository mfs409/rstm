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
  void initTM<OrecEagerHour>()
  {
       // set the name
      stms[OrecEagerHour].name      = "OrecEagerHour";

      // set the pointers
      stms[OrecEagerHour].begin     = OrecEagerGenericBegin<HourglassCM>;
      stms[OrecEagerHour].commit    = OrecEagerGenericCommit<HourglassCM>;
      stms[OrecEagerHour].rollback  = OrecEagerGenericRollback<HourglassCM>;
      stms[OrecEagerHour].read      = OrecEagerGenericRead<HourglassCM>;
      stms[OrecEagerHour].write     = OrecEagerGenericWrite<HourglassCM>;
      stms[OrecEagerHour].irrevoc   = OrecEagerGenericIrrevoc<HourglassCM>;
      stms[OrecEagerHour].switcher  = OrecEagerGenericOnSwitchTo<HourglassCM>;
      stms[OrecEagerHour].privatization_safe = false;
  }
}

#ifdef STM_ONESHOT_ALG_OrecEagerHour
DECLARE_AS_ONESHOT_SIMPLE(OrecEagerGeneric<HourglassCM>)
#endif
