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
  void initTM<ByEAUFCM>()
  {
      ByEAU_Generic<FCM>::Initialize(ByEAUFCM, "ByEAUFCM");
  }
}

#ifdef STM_ONESHOT_ALG_ByEAUFCM
DECLARE_AS_ONESHOT_NORMAL(ByEAU_Generic<FCM>)
#endif
