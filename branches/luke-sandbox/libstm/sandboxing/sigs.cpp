/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <sys/time.h>                   // setitimer
#include "common/interposition.hpp"     // stm::lazy_load_symbol
#include "common/utils.hpp"             // stm::typed_memcpy
#include "common/platform.hpp"          // stm::sync_fai
#include "stm/txthread.hpp"
#include "stm/metadata.hpp"
#include "ssigs/ssigs.h"
#include "sandboxing.hpp"
#include "timer.hpp"
#include "../policies/policies.hpp"     // curr_policy
#include "../algs/algs.hpp"             // stms[]

using stm::Self;
using stm::TxThread;
using stm::sync_fai;
using stm::typed_memcpy;
using stm::lazy_load_symbol;
using stm::stms;
using stm::curr_policy;
using stm::threadcount;
using stm::pad_word_t;
using stm::MAX_THREADS;
using stm::trans_nums;
using stm::threads;
using stm::sandbox::demultiplex_timer;

/**
 *  A thread local pointer to the stm-allocated alt stack.
 */
static __thread uint8_t* my_stack = NULL;

__thread int stm::sandbox::in_lib = 0;

stm::sandbox::InLib::InLib() {
    ++stm::sandbox::in_lib;
}

stm::sandbox::InLib::~InLib() {
    --stm::sandbox::in_lib;
}

/**
 *  Our signal handler (signal_shadowing_t).
 */
static void
prevalidate(int sig, siginfo_t* info, void* ctx, libc_sigaction_t cont)
{
    if (!stm::sandbox::in_lib && Self->scope && !Self->tmvalidate(Self)) {
        // we're not valid.... we'll need to abort, but only for the signals
        // that we expect to be dealing with.
        switch (sig) {
          case SIGSEGV:
          case SIGBUS:
          case SIGFPE:
          case SIGILL:
          case SIGABRT:
          case SIGUSR2:
            Self->tmabort(Self); // just abort... noreturn
          default:
            fprintf(stderr, "libstm: saw a signal we didn't expect %i", sig);
        }
    }
    // should be a tail call
    cont(sig, info, ctx);
}

static void
ping_the_world(int sig, TxThread* self)
{
    static volatile pad_word_t prev_trans[MAX_THREADS] = {{0, {0}}};
    static volatile sig_atomic_t pinging = 0;

    if (threadcount.val == 1)
        return;

    // if someone else is pinging, just continue
    if (sync_fai(&pinging))
        return;

    for (unsigned i = 0, e = threadcount.val; i < e; ++i) {
        if (!threads[i]->scope)         // or to a thread not in a transaction
            continue;
        if (trans_nums[i].val != prev_trans[i].val) // or progressing
            prev_trans[i].val = trans_nums[i].val;
        else
            pthread_kill(threads[i]->pthreadid, SIGUSR2);
    }
    pinging = 0;
}

static void
checktimer(int sig, siginfo_t* info, void* ctx, libc_sigaction_t cont)
{
    if (stms[curr_policy.ALG_ID].sandbox_signals) {
        ping_the_world(sig, Self);
    }

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
    sigaddset(&shadow.mask, SIGUSR2);

    // prevalidate the "handled" program errors
    const int handled[] = {
        SIGBUS,
        SIGFPE,
        SIGILL,
        SIGUSR2
    };

    for (int i = 0, e = length_of(handled); i < e; ++i)
        stm_shadow_sigaction(handled[i], shadow);

    // SIGSEGV has to run on an alternate stack
    shadow.flags |= SA_ONSTACK;
    sigaddset(&shadow.mask, SIGVTALRM);
    stm_shadow_sigaction(SIGSEGV, shadow);

    // timer handler
    shadow.action = checktimer;
    shadow.flags &= ~SA_ONSTACK;
    stm_shadow_sigaction(SIGVTALRM, shadow);
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
 *
 */
void
stm::sandbox::start_timer()
{
    // Start my validation timer.
    struct itimerval timer;

    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 1000000 / 10;
    timer.it_value = timer.it_interval;
    setitimer(ITIMER_VIRTUAL, &timer, NULL);
}

/**
 *
 */
void
stm::sandbox::stop_timer()
{
    // Start my validation timer.
    struct itimerval timer;

    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    timer.it_value = timer.it_interval;
    setitimer(ITIMER_VIRTUAL, &timer, NULL);
}

/**
 *  Sandboxing requires that we be prepared to run the SIGSEGV handler in
 *  low-stack conditions, which means that we need an altstack set up. This is
 *  called during stm::thread_init() and initializes that alt stack.
 */
void
stm::sandbox::init_thread()
{
    my_stack = new uint8_t[SIGSTKSZ];
    stack_t stack;
    stack.ss_sp = my_stack;
    stack.ss_flags = 0;
    stack.ss_size = SIGSTKSZ;
    call_sigaltstack(&stack, NULL);
}
