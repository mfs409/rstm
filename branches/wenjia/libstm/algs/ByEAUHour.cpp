/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "ByEAU.hpp"

namespace stm
{
  template <>
  void initTM<ByEAUHour>()
  {
      // set the name
      stms[ByEAUHour].name      = "ByEAUHour";
      stms[ByEAUHour].begin     = ByEAUGenericBegin<HourglassCM>;
      stms[ByEAUHour].commit    = ByEAUGenericCommitRO<HourglassCM>;
      stms[ByEAUHour].read      = ByEAUGenericReadRO<HourglassCM>;
      stms[ByEAUHour].write     = ByEAUGenericWriteRO<HourglassCM>;
      stms[ByEAUHour].rollback  = ByEAUGenericRollback<HourglassCM>;
      stms[ByEAUHour].irrevoc   = ByEAUGenericIrrevoc<HourglassCM>;
      stms[ByEAUHour].switcher  = ByEAUGenericOnSwitchTo<HourglassCM>;
      stms[ByEAUHour].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_ByEAUHour
DECLARE_AS_ONESHOT_NORMAL(ByEAUGeneric<HourglassCM>)
#endif
