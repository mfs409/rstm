#ifndef SSIGS_SSIGS_H
#define SSIGS_SSIGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <signal.h>

/// The mechanics of shadowing a handler are simple. The "shadowing handler"
/// looks lik a standard sigaction, but it gets the shadowed handler as an
/// additional parameter (actually, it gets some continuation as a parameter,
/// which it can think of as a shadowed handler).
///
/// The shadowing handler has three choices.
///
///   1) It can ignore the continuation and just return the caller, which
///      effectively terminates the signal handling process. An example of this
///      would be the timer handler that demultiplexes a timer and finds that
///      it's directed towards libstm. In this case, the user handler should
///      never be called.
///
///   2) It can simply call the continuation after its done whatever it needs
///      to do.
///
///   3) It can return via lonjmp or siglongjmp to some previous point in the
///      code.
///
/// The library intercepts the signal and sigaction calls, so the shadowing
/// system only needs to indicate that it's installing handlers (like libjsig)
/// and the library can deal with it.

typedef void (*libc_sigaction_t)(int, siginfo_t*, void*);

typedef struct {
    void (*action)(int, siginfo_t*, void*, libc_sigaction_t);
    sigset_t mask;
    int flags;
} stm_shadow_t;

void stm_shadow_sigaction(int, const stm_shadow_t&);

#ifdef __cplusplus
}
#endif

#endif // SSIGS_SSIGS_H
