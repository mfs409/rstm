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
  void initTM<ByEAUFCM>()
  {
      // set the name
      stms[ByEAUFCM].name      = "ByEAUFCM";
      stms[ByEAUFCM].begin     = ByEAUGenericBegin<FCM>;
      stms[ByEAUFCM].commit    = ByEAUGenericCommitRO<FCM>;
      stms[ByEAUFCM].read      = ByEAUGenericReadRO<FCM>;
      stms[ByEAUFCM].write     = ByEAUGenericWriteRO<FCM>;
      stms[ByEAUFCM].rollback  = ByEAUGenericRollback<FCM>;
      stms[ByEAUFCM].irrevoc   = ByEAUGenericIrrevoc<FCM>;
      stms[ByEAUFCM].switcher  = ByEAUGenericOnSwitchTo<FCM>;
      stms[ByEAUFCM].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_ByEAUFCM
DECLARE_AS_ONESHOT_NORMAL(ByEAUGeneric<FCM>)
#endif
