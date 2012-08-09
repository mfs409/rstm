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

namespace stm
{
  template <>
  void initTM<NOrecHour>()
  {
      NOrec_Generic<HourglassCM>::initialize(NOrecHour, "NOrecHour");
  }
}

#ifdef STM_ONESHOT_ALG_NOrecHour
DECLARE_AS_ONESHOT_NORMAL(NOrec_Generic<HourglassCM>)
#endif
