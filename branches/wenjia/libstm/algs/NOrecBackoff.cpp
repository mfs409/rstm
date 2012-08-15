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

DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(NOrec, NOrecBackoff, BackoffCM)
REGISTER_TEMPLATE_ALG(NOrec, NOrecBackoff, "NOrecBackoff", true, BackoffCM)

#ifdef STM_ONESHOT_ALG_NOrecBackoff
DECLARE_AS_ONESHOT_NORMAL(NOrecGeneric<BackoffCM>)
#endif
