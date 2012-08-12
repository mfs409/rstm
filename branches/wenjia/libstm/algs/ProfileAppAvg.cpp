/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "ProfileApp.hpp"

namespace stm
{
  /**
   *  ProfileAppAvg initialization
   */
  template<>
  void initTM<ProfileAppAvg>()
  {
      // set the name
      stms[ProfileAppAvg].name      = "ProfileAppAvg";
      stms[ProfileAppAvg].begin     = ProfileAppBegin<__AVERAGE>;
      stms[ProfileAppAvg].commit    = ProfileAppCommitRO<__AVERAGE>;
      stms[ProfileAppAvg].read      = ProfileAppReadRO<__AVERAGE>;
      stms[ProfileAppAvg].write     = ProfileAppWriteRO<__AVERAGE>;
      stms[ProfileAppAvg].rollback  = ProfileAppRollback<__AVERAGE>;
      stms[ProfileAppAvg].irrevoc   = ProfileAppIrrevoc<__AVERAGE>;
      stms[ProfileAppAvg].switcher  = ProfileAppOnSwitchTo<__AVERAGE>;
      stms[ProfileAppAvg].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_ProfileAppAvg
DECLARE_AS_ONESHOT_NORMAL(ProfileApp<__AVERAGE>)
#endif
