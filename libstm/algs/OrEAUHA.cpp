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
  void initTM<OrEAUHA>()
  {
      stms[OrEAUHA].name = "OrEAUHA";

      // set the pointers
      stms[OrEAUHA].begin     = OrEAUGenericBegin<HyperAggressiveCM>;
      stms[OrEAUHA].commit    = OrEAUGenericCommitRO<HyperAggressiveCM>;
      stms[OrEAUHA].read      = OrEAUGenericReadRO<HyperAggressiveCM>;
      stms[OrEAUHA].write     = OrEAUGenericWriteRO<HyperAggressiveCM>;
      stms[OrEAUHA].irrevoc   = OrEAUGenericIrrevoc<HyperAggressiveCM>;
      stms[OrEAUHA].switcher  = OrEAUGenericOnSwitchTo<HyperAggressiveCM>;
      stms[OrEAUHA].privatization_safe = false;
      stms[OrEAUHA].rollback  = OrEAUGenericRollback<HyperAggressiveCM>;
  }
}

#ifdef STM_ONESHOT_ALG_OrEAUHA
DECLARE_AS_ONESHOT_NORMAL(OrEAUGeneric<HyperAggressiveCM>)
#endif
