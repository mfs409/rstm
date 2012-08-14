/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "OrEAU.hpp"

namespace stm
{
  template <>
  void initTM<OrEAUHour>()
  {
      stms[OrEAUHour].name = "OrEAUHour";

      // set the pointers
      stms[OrEAUHour].begin     = OrEAUGenericBegin<HourglassCM>;
      stms[OrEAUHour].commit    = OrEAUGenericCommitRO<HourglassCM>;
      stms[OrEAUHour].read      = OrEAUGenericReadRO<HourglassCM>;
      stms[OrEAUHour].write     = OrEAUGenericWriteRO<HourglassCM>;
      stms[OrEAUHour].irrevoc   = OrEAUGenericIrrevoc<HourglassCM>;
      stms[OrEAUHour].switcher  = OrEAUGenericOnSwitchTo<HourglassCM>;
      stms[OrEAUHour].privatization_safe = false;
      stms[OrEAUHour].rollback  = OrEAUGenericRollback<HourglassCM>;
  }

}

#ifdef STM_ONESHOT_ALG_OrEAUHour
DECLARE_AS_ONESHOT_NORMAL(OrEAUGeneric<HourglassCM>)
#endif
