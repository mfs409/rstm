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

namespace stm
{
  template <>
  void initTM<ByEAUBackoff>()
  {
      ByEAU_Generic<BackoffCM>::Initialize(ByEAUBackoff, "ByEAUBackoff");
  }
}

#ifdef STM_ONESHOT_ALG_ByEAUBackoff
DECLARE_AS_ONESHOT_NORMAL(ByEAU_Generic<BackoffCM>)
#endif
