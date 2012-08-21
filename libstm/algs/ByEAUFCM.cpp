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

DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(ByEAU, ByEAUFCM, FCM)
REGISTER_TEMPLATE_ALG(ByEAU, ByEAUFCM, "ByEAUFCM", true, FCM)

#ifdef STM_ONESHOT_ALG_ByEAUFCM
DECLARE_AS_ONESHOT(ByEAUFCM)
#endif
