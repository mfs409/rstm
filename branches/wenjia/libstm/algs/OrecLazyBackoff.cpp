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

DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(OrecLazy, OrecLazyBackoff, BackoffCM)
REGISTER_TEMPLATE_ALG(OrecLazy, OrecLazyBackoff, "OrecLazyBackoff", false, BackoffCM)

#ifdef STM_ONESHOT_ALG_OrecLazyBackoff
DECLARE_AS_ONESHOT_NORMAL(OrecLazyGeneric<BackoffCM>)
#endif
