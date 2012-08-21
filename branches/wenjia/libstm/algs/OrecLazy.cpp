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

DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(OrecLazy, OrecLazy, HyperAggressiveCM)
REGISTER_TEMPLATE_ALG(OrecLazy, OrecLazy, "OrecLazy", false, HyperAggressiveCM)

#ifdef STM_ONESHOT_ALG_OrecLazy
DECLARE_AS_ONESHOT(OrecLazy)
#endif
