#include <cstddef>
#include <cassert>
#include <csignal>
#include "common/utils.hpp"
#include "stm/txthread.hpp"
#include "sandboxing.hpp"

using stm::length_of;
using stm::TxThread;
using stm::Self;
using stm::UNRECOVERABLE;

static const int N_HANDLERS = 32;

typedef struct sigaction sigaction_t;

/// We're using link-time interposition at the moment, and we occasionally want
/// to call the real versions of signal and sigaction, so here they are.
extern "C" sighandler_t __real_signal(int, sighandler_t);
extern "C" int __real_sigaction(int, const sigaction_t*, sigaction_t*);

static volatile int registering = 2;
static sigaction_t handlers[N_HANDLERS] = {{{0}}};
static bool libstm_handles[N_HANDLERS] = {0};

static void
check(int result)
{
    if (result)
        UNRECOVERABLE("Failed to install a signal handler");
}

extern "C" sighandler_t
__wrap_signal(int sig, sighandler_t handler)
{
    if (registering)
        UNRECOVERABLE("Can't call signal while libstm is registering.");

    if (libstm_handles[sig]) {
    }

    return __real_signal(sig, handler);
}

extern "C" int
__wrap_sigaction(int sig, const sigaction_t* handler, sigaction_t* old_out)
{
    if (registering) {
        libstm_handles[sig] = true;
        return __real_sigaction(sig, handler, old_out);
    }

    if (libstm_handles[sig]) {
        if (old_out)
            __builtin_memcpy(old_out, handlers + sig, sizeof(sigaction_t));
        if (handler)
            __builtin_memcpy(handlers + sig, handler, sizeof(sigaction_t));
        return 0;
    }

    return __real_sigaction(sig, handler, old_out);
}

static void
start_registration()
{
    int r = __sync_fetch_and_sub(&registering, 1);
    assert(r == 2);

    // record existing handlers
    for (int i = 1, e = length_of(handlers); i < e; ++i)
        check(__real_sigaction(i, NULL, handlers + i));
}

static void
end_registration()
{
    assert(registering == 1);
    --registering;
}

static void
validating_signal_handler(int sig, siginfo_t* i, void* v)
{
    stm::TxThread* self = &*stm::Self;
    if (!self->tmvalidate(self))
        self->tmabort(self);

    handlers[sig].sa_sigaction(sig, i, v);
}

/**
 *  Installs the signal handlers that sandboxing requires.
 */
void
stm::install_signal_handlers()
{
    const int handled[] = {
        SIGSEGV,
        SIGBUS,
        SIGFPE,
        SIGILL
    };

    struct sigaction sa;
    sa.sa_sigaction = validating_signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    start_registration();
    for (int i = 0, e = length_of(handled); i < e; ++i)
        __wrap_sigaction(handled[i], &sa, NULL);
    end_registration();
}
