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
    void initTM<OrecLazyBackoff>()
    {
      // set the name
      stms[OrecLazyBackoff].name      = "OrecLazyBackoff";

      // set the pointers
      stms[OrecLazyBackoff].begin     = OrecLazyGenericBegin<BackoffCM>;
      stms[OrecLazyBackoff].commit    = OrecLazyGenericCommitRO<BackoffCM>;
      stms[OrecLazyBackoff].rollback  = OrecLazyGenericRollback<BackoffCM>;
      stms[OrecLazyBackoff].read      = OrecLazyGenericReadRO<BackoffCM>;
      stms[OrecLazyBackoff].write     = OrecLazyGenericWriteRO<BackoffCM>;
      stms[OrecLazyBackoff].irrevoc   = OrecLazyGenericIrrevoc<BackoffCM>;
      stms[OrecLazyBackoff].switcher  = OrecLazyGenericOnSwitchTo<BackoffCM>;
      stms[OrecLazyBackoff].privatization_safe = false;
    }
}

#ifdef STM_ONESHOT_ALG_OrecLazyBackoff
DECLARE_AS_ONESHOT_NORMAL(OrecLazyGeneric<BackoffCM>)
#endif
