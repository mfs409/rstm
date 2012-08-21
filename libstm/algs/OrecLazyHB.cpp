/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "OrecLazy.hpp"

DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(OrecLazy, OrecLazyHB, HourglassBackoffCM)
REGISTER_TEMPLATE_ALG(OrecLazy, OrecLazyHB, "OrecLazyHB", false, HourglassBackoffCM)

#ifdef STM_ONESHOT_ALG_OrecLazyHB
DECLARE_AS_ONESHOT(OrecLazyHB)
#endif
