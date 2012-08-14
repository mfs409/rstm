/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "OrecLazy.hpp"

namespace stm
{
    template <>
    void initTM<OrecLazyHour>()
    {
      // set the name
      stms[OrecLazyHour].name      = "OrecLazyHour";

      // set the pointers
      stms[OrecLazyHour].begin     = OrecLazyGenericBegin<HourglassCM>;
      stms[OrecLazyHour].commit    = OrecLazyGenericCommitRO<HourglassCM>;
      stms[OrecLazyHour].rollback  = OrecLazyGenericRollback<HourglassCM>;
      stms[OrecLazyHour].read      = OrecLazyGenericReadRO<HourglassCM>;
      stms[OrecLazyHour].write     = OrecLazyGenericWriteRO<HourglassCM>;
      stms[OrecLazyHour].irrevoc   = OrecLazyGenericIrrevoc<HourglassCM>;
      stms[OrecLazyHour].switcher  = OrecLazyGenericOnSwitchTo<HourglassCM>;
      stms[OrecLazyHour].privatization_safe = false;
    }
}

#ifdef STM_ONESHOT_ALG_OrecLazyHour
DECLARE_AS_ONESHOT_NORMAL(OrecLazyGeneric<HourglassCM>)
#endif
