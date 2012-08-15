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

DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(OrEAU, OrEAUHA, HyperAggressiveCM)
REGISTER_TEMPLATE_ALG(OrEAU, OrEAUHA, "OrEAUHA", false, HyperAggressiveCM)

#ifdef STM_ONESHOT_ALG_OrEAUHA
DECLARE_AS_ONESHOT_NORMAL(OrEAUGeneric<HyperAggressiveCM>)
#endif
