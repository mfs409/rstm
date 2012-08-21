/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "NOrec.hpp"

DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(NOrec, NOrec, HyperAggressiveCM)
REGISTER_TEMPLATE_ALG(NOrec, NOrec, "NOrec", true, HyperAggressiveCM)

#ifdef STM_ONESHOT_ALG_NOrec
DECLARE_AS_ONESHOT(NOrec)
#endif
