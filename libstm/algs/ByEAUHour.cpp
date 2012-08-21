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

DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(ByEAU, ByEAUHour, HourglassCM)
REGISTER_TEMPLATE_ALG(ByEAU, ByEAUHour, "ByEAUHour", true, HourglassCM)

#ifdef STM_ONESHOT_ALG_ByEAUHour
DECLARE_AS_ONESHOT(ByEAUHour)
#endif
