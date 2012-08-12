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
   *  ProfileAppAll initialization
   */
  template<>
  void initTM<ProfileAppAll>()
  {
      // set the name
      stms[ProfileAppAll].name      = "ProfileAppAll";
      stms[ProfileAppAll].begin     = ProfileAppBegin<__AVERAGE>;
      stms[ProfileAppAll].commit    = ProfileAppCommitRO<__AVERAGE>;
      stms[ProfileAppAll].read      = ProfileAppReadRO<__AVERAGE>;
      stms[ProfileAppAll].write     = ProfileAppWriteRO<__AVERAGE>;
      stms[ProfileAppAll].rollback  = ProfileAppRollback<__AVERAGE>;
      stms[ProfileAppAll].irrevoc   = ProfileAppIrrevoc<__AVERAGE>;
      stms[ProfileAppAll].switcher  = ProfileAppOnSwitchTo<__AVERAGE>;
      stms[ProfileAppAll].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_ProfileAppAll
DECLARE_AS_ONESHOT_NORMAL(ProfileApp<__AVERAGE>)
#endif
