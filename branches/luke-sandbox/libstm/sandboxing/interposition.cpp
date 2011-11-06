/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

/**
 *  We need to sit in front of attempts by the client application to register
 *  signal handlers. We currently do this using link-time symbol wrapping. This
 *  isn't the best option because it requires the client to use the
 *  -Wl,wrap,symbol flags during linking.
 *
 *  There's probably a way to do this using a linker script, but I don't know
 *  how yet.
 */

#include <cstddef>
#include <cassert>
#include "handlers.hpp"

extern "C" sighandler_t __real_signal(int, sighandler_t);
extern "C" sighandler_t __wrap_signal(int, sighandler_t);

extern "C" int __real_sigaction(int, const sigaction_t*, sigaction_t*);
extern "C" int __wrap_sigaction(int, const sigaction_t*, sigaction_t*);

/**
 *  The client is registering a sighandler.
 */
sighandler_t
__wrap_signal(int sig, sighandler_t handler)
{
    return __real_signal(sig, handler);
}

/**
 *  The client is registering a sigaction.
 */
int
__wrap_sigaction(int sig, const sigaction_t* install, sigaction_t* old_out)
{
    return __real_sigaction(sig, install, old_out);
}

/**
 *  The library is registering a sigaction.
 */
void
libstm_internal_sigaction(int signal, const sigaction_t* install)
{
    if (__real_sigaction(signal, install, NULL))
        assert(false && "failed to register signal");
}
