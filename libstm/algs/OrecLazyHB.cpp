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
    void initTM<OrecLazyHB>()
    {
      // set the name
      stms[OrecLazyHB].name      = "OrecLazyHB";

      // set the pointers
      stms[OrecLazyHB].begin     = OrecLazyGenericBegin<HourglassBackoffCM>;
      stms[OrecLazyHB].commit    = OrecLazyGenericCommitRO<HourglassBackoffCM>;
      stms[OrecLazyHB].rollback  = OrecLazyGenericRollback<HourglassBackoffCM>;
      stms[OrecLazyHB].read      = OrecLazyGenericReadRO<HourglassBackoffCM>;
      stms[OrecLazyHB].write     = OrecLazyGenericWriteRO<HourglassBackoffCM>;
      stms[OrecLazyHB].irrevoc   = OrecLazyGenericIrrevoc<HourglassBackoffCM>;
      stms[OrecLazyHB].switcher  = OrecLazyGenericOnSwitchTo<HourglassBackoffCM>;
      stms[OrecLazyHB].privatization_safe = false;
    }
}

#ifdef STM_ONESHOT_ALG_OrecLazyHB
DECLARE_AS_ONESHOT_NORMAL(OrecLazyGeneric<HourglassBackoffCM>)
#endif
