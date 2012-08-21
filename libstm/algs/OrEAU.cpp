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

DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(OrEAU, OrEAU, BackoffCM)
REGISTER_TEMPLATE_ALG(OrEAU, OrEAU, "OrEAU", false, BackoffCM)

#ifdef STM_ONESHOT_ALG_OrEAU
DECLARE_AS_ONESHOT(OrEAU)
#endif
