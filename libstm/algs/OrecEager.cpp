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
  void initTM<OrecEager>()
  {
      OrecEager_Generic<HyperAggressiveCM>::initialize(OrecEager, "OrecEager");
  }
}

#ifdef STM_ONESHOT_ALG_OrecEager
DECLARE_AS_ONESHOT_SIMPLE(OrecEager_Generic<HyperAggressiveCM>)
#endif
