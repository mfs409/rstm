/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <cstdlib>
#include "handlers.hpp"

void
validate_synchronous_signal(int, siginfo_t*, void*)
{
    _Exit(-1);
}
