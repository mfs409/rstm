/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef LIBSTM_SANDBOXING_HANDLERS_HPP
#define LIBSTM_SANDBOXING_HANDLERS_HPP

#ifdef __cplusplus
extern "C" {
#endif

#include <signal.h>

    typedef struct sigaction sigaction_t;

    /**
     *  We use this internally for registering handlers. It allows us to
     *  distinguish between the libstm sigactions and the client's interposed
     *  sigactions without much work.
     */
    void libstm_internal_sigaction(int, const sigaction_t*);

    /**
     *  This is a general purpose handler that checks to see if the signal
     *  occurred in a transaction, and if it did it calls TxThread::tmvalidate
     *  before delivering the signal. If validation fails the signal is
     *  suppressed and we abort.
     */
    void validate_synchronous_signal(int, siginfo_t*, void*);

#ifdef __cplusplus
}
#endif

#endif // LIBSTM_SANDBOXING_HANDLERS_HPP
