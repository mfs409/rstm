/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "OrecEager.hpp"

DECLARE_SIMPLE_METHODS_FROM_SIMPLE_TEMPLATE(OrecEager, OrecEagerHB, HourglassBackoffCM)
REGISTER_SIMPLE_TEMPLATE_ALG(OrecEager, OrecEagerHB, "OrecEagerHB", false, HourglassBackoffCM)

#ifdef STM_ONESHOT_ALG_OrecEagerHB
DECLARE_AS_ONESHOT(OrecEagerHB)
#endif
