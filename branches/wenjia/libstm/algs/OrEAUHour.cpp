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
  void initTM<OrEAUHour>()
  {
      OrEAU_Generic<HourglassCM>::initialize(OrEAUHour, "OrEAUHour");
  }

}

#ifdef STM_ONESHOT_ALG_OrEAUHour
DECLARE_AS_ONESHOT_NORMAL(OrEAU_Generic<HourglassCM>)
#endif
