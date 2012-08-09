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
  void initTM<NOrec>()
  {
      NOrec_Generic<HyperAggressiveCM>::initialize(NOrec, "NOrec");
  }
}

#ifdef STM_ONESHOT_ALG_NOrec
DECLARE_AS_ONESHOT_NORMAL(NOrec_Generic<HyperAggressiveCM>)
#endif
