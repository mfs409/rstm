/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "OrecEager.hpp"

namespace stm
{
  template <>
  void initTM<OrecEagerHour>()
  {
      OrecEager_Generic<HourglassCM>::initialize(OrecEagerHour, "OrecEagerHour");
  }
}

#ifdef STM_ONESHOT_ALG_OrecEagerHour
DECLARE_AS_ONESHOT_SIMPLE(OrecEager_Generic<HourglassCM>)
#endif
