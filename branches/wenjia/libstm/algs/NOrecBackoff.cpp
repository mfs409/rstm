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
  void initTM<NOrecBackoff>()
  {
      // set the name
      stms[NOrecBackoff].name = "NOrecBackoff";

      // set the pointers
      stms[NOrecBackoff].begin     = NOrecGenericBegin<BackoffCM>;
      stms[NOrecBackoff].commit    = NOrecGenericCommitRO<BackoffCM>;
      stms[NOrecBackoff].read      = NOrecGenericReadRO<BackoffCM>;
      stms[NOrecBackoff].write     = NOrecGenericWriteRO<BackoffCM>;
      stms[NOrecBackoff].irrevoc   = NOrecGenericIrrevoc<BackoffCM>;
      stms[NOrecBackoff].switcher  = NOrecGenericOnSwitchTo<BackoffCM>;
      stms[NOrecBackoff].privatization_safe = true;
      stms[NOrecBackoff].rollback  = NOrecGenericRollback<BackoffCM>;
  }
}

#ifdef STM_ONESHOT_ALG_NOrecBackoff
DECLARE_AS_ONESHOT_NORMAL(NOrecGeneric<BackoffCM>)
#endif
