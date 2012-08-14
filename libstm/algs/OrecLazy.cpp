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
    void initTM<OrecLazy>()
    {
      // set the name
      stms[OrecLazy].name      = "OrecLazy";

      // set the pointers
      stms[OrecLazy].begin     = OrecLazyGenericBegin<HyperAggressiveCM>;
      stms[OrecLazy].commit    = OrecLazyGenericCommitRO<HyperAggressiveCM>;
      stms[OrecLazy].rollback  = OrecLazyGenericRollback<HyperAggressiveCM>;
      stms[OrecLazy].read      = OrecLazyGenericReadRO<HyperAggressiveCM>;
      stms[OrecLazy].write     = OrecLazyGenericWriteRO<HyperAggressiveCM>;
      stms[OrecLazy].irrevoc   = OrecLazyGenericIrrevoc<HyperAggressiveCM>;
      stms[OrecLazy].switcher  = OrecLazyGenericOnSwitchTo<HyperAggressiveCM>;
      stms[OrecLazy].privatization_safe = false;
    }
}

#ifdef STM_ONESHOT_ALG_OrecLazy
DECLARE_AS_ONESHOT_NORMAL(OrecLazyGeneric<HyperAggressiveCM>)
#endif
