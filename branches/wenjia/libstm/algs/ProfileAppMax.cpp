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
   *  ProfileAppMax initialization
   */
  template<>
  void initTM<ProfileAppMax>()
  {
      // set the name
      stms[ProfileAppMax].name      = "ProfileAppMax";
      stms[ProfileAppMax].begin     = ProfileAppBegin<__MAXIMUM>;
      stms[ProfileAppMax].commit    = ProfileAppCommitRO<__MAXIMUM>;
      stms[ProfileAppMax].read      = ProfileAppReadRO<__MAXIMUM>;
      stms[ProfileAppMax].write     = ProfileAppWriteRO<__MAXIMUM>;
      stms[ProfileAppMax].rollback  = ProfileAppRollback<__MAXIMUM>;
      stms[ProfileAppMax].irrevoc   = ProfileAppIrrevoc<__MAXIMUM>;
      stms[ProfileAppMax].switcher  = ProfileAppOnSwitchTo<__MAXIMUM>;
      stms[ProfileAppMax].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_ProfileAppMax
DECLARE_AS_ONESHOT_NORMAL(ProfileApp<__MAXIMUM>)
#endif
