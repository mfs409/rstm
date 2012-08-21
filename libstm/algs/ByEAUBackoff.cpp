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

DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(ByEAU, ByEAUBackoff, BackoffCM)
REGISTER_TEMPLATE_ALG(ByEAU, ByEAUBackoff, "ByEAUBackoff", true, BackoffCM)

#ifdef STM_ONESHOT_ALG_ByEAUBackoff
DECLARE_AS_ONESHOT(ByEAUBackoff)
#endif
