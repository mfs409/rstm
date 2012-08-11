/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <string.h>
#include "Registration.hpp"

/*** Store descriptions of the STM algorithms */
stm::alg_t stm::stms[stm::ALG_MAX];

/*** Use the stms array to map a string name to an algorithm ID */
int stm::stm_name_map(const char* phasename)
{
    for (int i = 0; i < ALG_MAX; ++i)
        if (0 == strcmp(phasename, stms[i].name))
            return i;
    return -1;
}
