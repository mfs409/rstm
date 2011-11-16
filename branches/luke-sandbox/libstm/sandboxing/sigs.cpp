/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "common/interposition.hpp"     // stm::lazy_load_symbol
#include "common/utils.hpp"             // stm::typed_memcpy
#include "stm/txthread.hpp"
#include "ssigs/ssigs.h"
#include "sandboxing.hpp"
#include "timer.hpp"

using stm::Self;
using stm::typed_memcpy;
using stm::lazy_load_symbol;
using stm::sandbox::demultiplex_timer;

/**
 *  Our signal handler (signal_shadowing_t).
 */
static void
prevalidate(int sig, siginfo_t* info, void* ctx, libc_sigaction_t cont)
{
    if (Self->scope && !Self->tmvalidate(Self)) {
        // we're not valid.... we'll need to abort, but only for the signals
        // that we expect to be dealing with.
        switch (sig) {
          case SIGSEGV:
          case SIGBUS:
          case SIGFPE:
          case SIGILL:
          case SIGABRT:
            Self->tmabort(Self); // just abort... noreturn
          default:
            fprintf(stderr, "libstm: saw a signal we didn't expect %i", sig);
        }
    }
    // should be a tail call
    cont(sig, info, ctx);
}

static void
checktimer(int sig, siginfo_t* info, void* ctx, libc_sigaction_t cont)
{
    if (!demultiplex_timer(sig, info, ctx))
        cont(sig, info, ctx);
}

/**
 *  Installs the signal handlers that sandboxing requires. Also prepare our
 *  thread-local alt stack for sigsegv processing.
 */
void
stm::sandbox::init_system()
{
    stm_shadow_t shadow;
    shadow.action = prevalidate;
    shadow.flags = SA_SIGINFO;
    sigemptyset(&shadow.mask);

    // prevalidate the "handled" program errors
    const int handled[] = {
        SIGBUS,
        SIGFPE,
        SIGILL,
        SIGABRT
    };

    for (int i = 0, e = length_of(handled); i < e; ++i)
        stm_shadow_sigaction(handled[i], shadow);

    // SIGSEGV has to run on an alternate stack
    shadow.flags |= SA_ONSTACK;
    stm_shadow_sigaction(SIGSEGV, shadow);

    // Timer management.
    shadow.action = checktimer;
    shadow.flags &= ~SA_ONSTACK;
    stm_shadow_sigaction(SIGALRM, shadow);
}

/**
 *  Lazy binding for library call interposition.
 */
static int
call_sigaltstack(const stack_t *ss, stack_t *oss)
{
    static int (*psigaltstack)(const stack_t*, stack_t*) = NULL;
    lazy_load_symbol(psigaltstack, "sigaltstack");
    return psigaltstack(ss, oss);
}

/**
 *  A thread local pointer to the stm-allocated alt stack.
 */
static __thread uint8_t* my_stack = NULL;

/**
 *  If the user tries to register an alt stack, we'll want to use it. Just
 *  check to see if the old alt stack was our stm-allocated one, and if it was
 *  then free it.
 */
extern "C" int
sigaltstack(const stack_t *ss, stack_t *oss)
{
    stack_t stack;
    int r = call_sigaltstack(ss, &stack);

    if (stack.ss_sp == my_stack) {
        delete[] my_stack;
        stack.ss_sp = NULL;
        stack.ss_flags = 0;
        stack.ss_size = 0;
    }

    if (oss)
        typed_memcpy(oss, &stack);

    return r;
}

/**
 *  Sandboxing requires that we be prepared to run the SIGSEGV handler in
 *  low-stack conditions, which means that we need an altstack set up. This is
 *  called during stm::thread_init() and initializes that alt stack.
 */
void
stm::sandbox::init_thread()
{
    my_stack = new uint8_t[MINSIGSTKSZ];
    stack_t stack;
    stack.ss_sp = my_stack;
    stack.ss_flags = 0;
    stack.ss_size = MINSIGSTKSZ;
    call_sigaltstack(&stack, NULL);
}
