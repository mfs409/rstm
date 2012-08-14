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
  void initTM<OrEAU>()
  {
      stms[OrEAU].name = "OrEAU";

      // set the pointers
      stms[OrEAU].begin     = OrEAUGenericBegin<BackoffCM>;
      stms[OrEAU].commit    = OrEAUGenericCommitRO<BackoffCM>;
      stms[OrEAU].read      = OrEAUGenericReadRO<BackoffCM>;
      stms[OrEAU].write     = OrEAUGenericWriteRO<BackoffCM>;
      stms[OrEAU].irrevoc   = OrEAUGenericIrrevoc<BackoffCM>;
      stms[OrEAU].switcher  = OrEAUGenericOnSwitchTo<BackoffCM>;
      stms[OrEAU].privatization_safe = false;
      stms[OrEAU].rollback  = OrEAUGenericRollback<BackoffCM>;
  }
}

#ifdef STM_ONESHOT_ALG_OrEAU
DECLARE_AS_ONESHOT_NORMAL(OrEAUGeneric<BackoffCM>)
#endif
