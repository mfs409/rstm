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

DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(ProfileApp, ProfileAppMax, __MAXIMUM)
REGISTER_TEMPLATE_ALG(ProfileApp, ProfileAppMax, "ProfileAppMax", true, __MAXIMUM)

#ifdef STM_ONESHOT_ALG_ProfileAppMax
DECLARE_AS_ONESHOT_NORMAL(ProfileApp<__MAXIMUM>)
#endif
