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
  template<>
  void initTM<ByEAUHA>()
  {
      // set the name
      stms[ByEAUHA].name      = "ByEAUHA";
      stms[ByEAUHA].begin     = ByEAUGenericBegin<HyperAggressiveCM>;
      stms[ByEAUHA].commit    = ByEAUGenericCommitRO<HyperAggressiveCM>;
      stms[ByEAUHA].read      = ByEAUGenericReadRO<HyperAggressiveCM>;
      stms[ByEAUHA].write     = ByEAUGenericWriteRO<HyperAggressiveCM>;
      stms[ByEAUHA].rollback  = ByEAUGenericRollback<HyperAggressiveCM>;
      stms[ByEAUHA].irrevoc   = ByEAUGenericIrrevoc<HyperAggressiveCM>;
      stms[ByEAUHA].switcher  = ByEAUGenericOnSwitchTo<HyperAggressiveCM>;
      stms[ByEAUHA].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_ByEAUHA
DECLARE_AS_ONESHOT_NORMAL(ByEAUGeneric<HyperAggressiveCM>)
#endif
