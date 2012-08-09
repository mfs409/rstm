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
    void initTM<OrecLazyHB>() {
        OrecLazy_Generic<HourglassBackoffCM>::Initialize(OrecLazyHB, "OrecLazyHB");
    }
}

#ifdef STM_ONESHOT_ALG_OrecLazyHB
DECLARE_AS_ONESHOT_NORMAL(OrecLazy_Generic<HourglassBackoffCM>)
#endif
