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
  void initTM<OrEAUFCM>()
  {
      stms[OrEAUFCM].name = "OrEAUFCM";

      // set the pointers
      stms[OrEAUFCM].begin     = OrEAUGenericBegin<FCM>;
      stms[OrEAUFCM].commit    = OrEAUGenericCommitRO<FCM>;
      stms[OrEAUFCM].read      = OrEAUGenericReadRO<FCM>;
      stms[OrEAUFCM].write     = OrEAUGenericWriteRO<FCM>;
      stms[OrEAUFCM].irrevoc   = OrEAUGenericIrrevoc<FCM>;
      stms[OrEAUFCM].switcher  = OrEAUGenericOnSwitchTo<FCM>;
      stms[OrEAUFCM].privatization_safe = false;
      stms[OrEAUFCM].rollback  = OrEAUGenericRollback<FCM>;
  }

}

#ifdef STM_ONESHOT_ALG_OrEAUFCM
DECLARE_AS_ONESHOT_NORMAL(OrEAUGeneric<FCM>)
#endif
