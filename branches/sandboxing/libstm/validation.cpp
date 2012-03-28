/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *   and
 *  Lehigh University Department of Computer Science and Engineering
 *
 *  License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "stm/lib_globals.hpp"
#include "stm/txthread.hpp"
#include "algs/algs.hpp"
#include "policies/policies.hpp"

using stm::Self;
using stm::stms;
using stm::curr_policy;

void
stm_validation_full() {
    if (!stms[curr_policy.ALG_ID].sandbox_signals)
        return;

    if (!Self->tmvalidate(Self))
        Self->tmabort(Self);
}
