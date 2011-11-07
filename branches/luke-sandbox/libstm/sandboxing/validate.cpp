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
#include "stm/txthread.hpp"

void
validate_synchronous_signal(int sig, siginfo_t*, void*)
{
    if (!stm::TxThread::tmvalidate(&*stm::Self))
        stm::TxThread::tmabort(&*stm::Self);
    else
        fprintf(stderr, "validated in signal handler");
        _Exit(-1);
}
