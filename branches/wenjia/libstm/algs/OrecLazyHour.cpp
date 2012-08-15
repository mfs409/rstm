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

DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(OrecLazy, OrecLazyHour, HourglassCM)
REGISTER_TEMPLATE_ALG(OrecLazy, OrecLazyHour, "OrecLazyHour", false, HourglassCM)

#ifdef STM_ONESHOT_ALG_OrecLazyHour
DECLARE_AS_ONESHOT_NORMAL(OrecLazyGeneric<HourglassCM>)
#endif
