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
  void initTM<ByEAUHA>()
  {
      ByEAU_Generic<HyperAggressiveCM>::Initialize(ByEAUHA, "ByEAUHA");
  }
}

#ifdef STM_ONESHOT_ALG_ByEAUHA
DECLARE_AS_ONESHOT_NORMAL(ByEAU_Generic<HyperAggressiveCM>)
#endif
