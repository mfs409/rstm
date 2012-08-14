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
  void initTM<OrecEager>()
  {
      // set the name
      stms[OrecEager].name      = "OrecEager";

      // set the pointers
      stms[OrecEager].begin     = OrecEagerGenericBegin<HyperAggressiveCM>;
      stms[OrecEager].commit    = OrecEagerGenericCommit<HyperAggressiveCM>;
      stms[OrecEager].rollback  = OrecEagerGenericRollback<HyperAggressiveCM>;
      stms[OrecEager].read      = OrecEagerGenericRead<HyperAggressiveCM>;
      stms[OrecEager].write     = OrecEagerGenericWrite<HyperAggressiveCM>;
      stms[OrecEager].irrevoc   = OrecEagerGenericIrrevoc<HyperAggressiveCM>;
      stms[OrecEager].switcher  = OrecEagerGenericOnSwitchTo<HyperAggressiveCM>;
      stms[OrecEager].privatization_safe = false;
  }
}

#ifdef STM_ONESHOT_ALG_OrecEager
DECLARE_AS_ONESHOT_SIMPLE(OrecEagerGeneric<HyperAggressiveCM>)
#endif
