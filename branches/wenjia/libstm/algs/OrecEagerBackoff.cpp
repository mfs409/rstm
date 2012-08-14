/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "OrecEager.hpp"

namespace stm
{
  template <>
  void initTM<OrecEagerBackoff>()
  {
       // set the name
      stms[OrecEagerBackoff].name      = "OrecEagerBackoff";

      // set the pointers
      stms[OrecEagerBackoff].begin     = OrecEagerGenericBegin<BackoffCM>;
      stms[OrecEagerBackoff].commit    = OrecEagerGenericCommit<BackoffCM>;
      stms[OrecEagerBackoff].rollback  = OrecEagerGenericRollback<BackoffCM>;
      stms[OrecEagerBackoff].read      = OrecEagerGenericRead<BackoffCM>;
      stms[OrecEagerBackoff].write     = OrecEagerGenericWrite<BackoffCM>;
      stms[OrecEagerBackoff].irrevoc   = OrecEagerGenericIrrevoc<BackoffCM>;
      stms[OrecEagerBackoff].switcher  = OrecEagerGenericOnSwitchTo<BackoffCM>;
      stms[OrecEagerBackoff].privatization_safe = false;
  }
}

#ifdef STM_ONESHOT_ALG_OrecEagerBackoff
DECLARE_AS_ONESHOT_SIMPLE(OrecEagerGeneric<BackoffCM>)
#endif
