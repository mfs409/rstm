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

DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(OrEAU, OrEAUFCM, FCM)
REGISTER_TEMPLATE_ALG(OrEAU, OrEAUFCM, "OrEAUFCM", false, FCM)

#ifdef STM_ONESHOT_ALG_OrEAUFCM
DECLARE_AS_ONESHOT_NORMAL(OrEAUGeneric<FCM>)
#endif
