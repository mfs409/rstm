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
  void initTM<OrecEagerHB>()
  {
      OrecEager_Generic<HourglassBackoffCM>::initialize(OrecEagerHB, "OrecEagerHB");
  }
}

#ifdef STM_ONESHOT_ALG_OrecEagerHB
DECLARE_AS_ONESHOT_SIMPLE(OrecEager_Generic<HourglassBackoffCM>)
#endif
