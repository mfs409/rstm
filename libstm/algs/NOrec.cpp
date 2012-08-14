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
  void initTM<NOrec>()
  {
      // set the name
      stms[NOrec].name = "NOrec";

      // set the pointers
      stms[NOrec].begin     = NOrecGenericBegin<HyperAggressiveCM>;
      stms[NOrec].commit    = NOrecGenericCommitRO<HyperAggressiveCM>;
      stms[NOrec].read      = NOrecGenericReadRO<HyperAggressiveCM>;
      stms[NOrec].write     = NOrecGenericWriteRO<HyperAggressiveCM>;
      stms[NOrec].irrevoc   = NOrecGenericIrrevoc<HyperAggressiveCM>;
      stms[NOrec].switcher  = NOrecGenericOnSwitchTo<HyperAggressiveCM>;
      stms[NOrec].privatization_safe = true;
      stms[NOrec].rollback  = NOrecGenericRollback<HyperAggressiveCM>;
  }
}

#ifdef STM_ONESHOT_ALG_NOrec
DECLARE_AS_ONESHOT_NORMAL(NOrecGeneric<HyperAggressiveCM>)
#endif
