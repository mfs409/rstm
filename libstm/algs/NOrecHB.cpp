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
  void initTM<NOrecHB>()
  {
      // set the name
      stms[NOrecHB].name = "NOrecHB";

      // set the pointers
      stms[NOrecHB].begin     = NOrecGenericBegin<HourglassBackoffCM>;
      stms[NOrecHB].commit    = NOrecGenericCommitRO<HourglassBackoffCM>;
      stms[NOrecHB].read      = NOrecGenericReadRO<HourglassBackoffCM>;
      stms[NOrecHB].write     = NOrecGenericWriteRO<HourglassBackoffCM>;
      stms[NOrecHB].irrevoc   = NOrecGenericIrrevoc<HourglassBackoffCM>;
      stms[NOrecHB].switcher  = NOrecGenericOnSwitchTo<HourglassBackoffCM>;
      stms[NOrecHB].privatization_safe = true;
      stms[NOrecHB].rollback  = NOrecGenericRollback<HourglassBackoffCM>;
  }
}

#ifdef STM_ONESHOT_ALG_NOrecHB
DECLARE_AS_ONESHOT_NORMAL(NOrecGeneric<HourglassBackoffCM>)
#endif
