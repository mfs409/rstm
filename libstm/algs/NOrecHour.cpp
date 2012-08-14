/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "NOrec.hpp"

namespace stm
{
  template <>
  void initTM<NOrecHour>()
  {
      // set the name
      stms[NOrecHour].name = "NOrecHour";

      // set the pointers
      stms[NOrecHour].begin     = NOrecGenericBegin<HourglassCM>;
      stms[NOrecHour].commit    = NOrecGenericCommitRO<HourglassCM>;
      stms[NOrecHour].read      = NOrecGenericReadRO<HourglassCM>;
      stms[NOrecHour].write     = NOrecGenericWriteRO<HourglassCM>;
      stms[NOrecHour].irrevoc   = NOrecGenericIrrevoc<HourglassCM>;
      stms[NOrecHour].switcher  = NOrecGenericOnSwitchTo<HourglassCM>;
      stms[NOrecHour].privatization_safe = true;
      stms[NOrecHour].rollback  = NOrecGenericRollback<HourglassCM>;
  }
}

#ifdef STM_ONESHOT_ALG_NOrecHour
DECLARE_AS_ONESHOT_NORMAL(NOrecGeneric<HourglassCM>)
#endif
