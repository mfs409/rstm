/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <signal.h>

extern "C"
{
    typedef struct sigaction sigaction_t;

    sighandler_t __real_signal(int, sighandler_t);
    int __real_sigaction(int, const sigaction_t*, sigaction_t*);

    sighandler_t __wrap_signal(int, sighandler_t);
    int __wrap_sigaction(int, const sigaction_t*, sigaction_t*);
}

sighandler_t
__wrap_signal(int sig, sighandler_t handler)
{
    return __real_signal(sig, handler);
}

int
__wrap_sigaction(int sig, const sigaction_t* install, sigaction_t* old_out)
{
    return __real_sigaction(sig, install, old_out);
}
