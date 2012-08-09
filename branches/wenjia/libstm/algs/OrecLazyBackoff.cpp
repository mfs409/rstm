/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "OrecLazy.hpp"

namespace stm
{
    template <>
    void initTM<OrecLazyBackoff>() {
        OrecLazy_Generic<BackoffCM>::Initialize(OrecLazyBackoff, "OrecLazyBackoff");
    }
}

#ifdef STM_ONESHOT_ALG_OrecLazyBackoff
DECLARE_AS_ONESHOT_NORMAL(OrecLazy_Generic<BackoffCM>)
#endif
