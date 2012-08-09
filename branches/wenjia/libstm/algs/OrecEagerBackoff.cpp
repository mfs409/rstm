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
  void initTM<OrecEagerBackoff>()
  {
      OrecEager_Generic<BackoffCM>::initialize(OrecEagerBackoff, "OrecEagerBackoff");
  }
}

#ifdef STM_ONESHOT_ALG_OrecEagerBackoff
DECLARE_AS_ONESHOT_SIMPLE(OrecEager_Generic<BackoffCM>)
#endif
