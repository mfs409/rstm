/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "OrEAU.hpp"

namespace stm
{
  template <>
  void initTM<OrEAUFCM>()
  {
      OrEAU_Generic<FCM>::initialize(OrEAUFCM, "OrEAUFCM");
  }

}

#ifdef STM_ONESHOT_ALG_OrEAUFCM
DECLARE_AS_ONESHOT_NORMAL(OrEAU_Generic<FCM>)
#endif
