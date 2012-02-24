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

void
stm_validation_full() {
    ++stm::Self->validations;
    if (!stm::Self->tmvalidate(stm::Self))
        stm::Self->tmabort(stm::Self);
}
