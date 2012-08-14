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
  template <>
  void initTM<ByEAUBackoff>()
  {
      // set the name
      stms[ByEAUBackoff].name      = "ByEAUBackoff";
      stms[ByEAUBackoff].begin     = ByEAUGenericBegin<BackoffCM>;
      stms[ByEAUBackoff].commit    = ByEAUGenericCommitRO<BackoffCM>;
      stms[ByEAUBackoff].read      = ByEAUGenericReadRO<BackoffCM>;
      stms[ByEAUBackoff].write     = ByEAUGenericWriteRO<BackoffCM>;
      stms[ByEAUBackoff].rollback  = ByEAUGenericRollback<BackoffCM>;
      stms[ByEAUBackoff].irrevoc   = ByEAUGenericIrrevoc<BackoffCM>;
      stms[ByEAUBackoff].switcher  = ByEAUGenericOnSwitchTo<BackoffCM>;
      stms[ByEAUBackoff].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_ByEAUBackoff
DECLARE_AS_ONESHOT_NORMAL(ByEAUGeneric<BackoffCM>)
#endif
