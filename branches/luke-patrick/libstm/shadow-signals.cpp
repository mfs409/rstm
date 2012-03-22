/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *   and
 *  Lehigh University Department of Computer Science and Engineering
 *
 *  License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

/// Signal virtualization requires that we emulate some of the standard signal
/// functionality. In particular, we need to shadow the registered signals and
/// intercept and emulate calls to signal and sigaction.
///
/// We also need to provide a mechanism for the client to "really" register a
/// signal. That;s what the ssigs_shadow function from include/ssigs/ssigs.h
/// does.
///
/// Finally, we need to be prepared to shadow a SIGSEGV handler when the user's
/// handler doesn't request SA_ONSTACK.

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include <stdint.h>                     // UINTPTR_MAX (c++1X for cstdint)
#include <cstdlib>                      // _Exit
#include "shadow-signals.h"
#include "interposition.hpp"            // stm::lazy_load_symbol
#include "common/platform.hpp"          // bcasptr
#include "common/locks.hpp"             // spin64()
#include "common/utils.hpp"             // stm::typed_memcpy

using stm::typed_memcpy;
using stm::lazy_load_symbol;

static const int NSIGS = 32;

/// ---------------------------------------------------------------------------
/// Used to call the "real" dynamically loaded sigaction.
/// ---------------------------------------------------------------------------
static int
call_sigaction(int sig, const struct sigaction* act, struct sigaction* old)
{
    typedef struct sigaction sigaction_t;
    static int (*psigaction)(int, const sigaction_t*, sigaction_t*) = NULL;
    lazy_load_symbol(psigaction, "sigaction");
    return psigaction(sig, act, old);
}

/// ---------------------------------------------------------------------------
/// Used to call the "real" dynamically loaded signal.
/// ---------------------------------------------------------------------------
static sighandler_t
call_signal(int sig, sighandler_t handler)
{
    static sighandler_t (*psignal)(int, sighandler_t) = NULL;
    lazy_load_symbol(psignal, "signal");
    return psignal(sig, handler);
}

static void do_shadowed_signal(int);
static void do_shadowed_signal_cont(int, siginfo_t*, void*);
static void do_shadowed_sigaction(int, siginfo_t*, void*);
static void do_shadowed_sigaction_cont(int, siginfo_t*, void*);

namespace {
/// ---------------------------------------------------------------------------
/// Stick all of the stuff required for a single signal into a record so that
/// we can allocate an array of them statically (don't really want to malloc
/// here because we need to do some modifications in signal handlers where
/// malloc may not work).
/// ---------------------------------------------------------------------------
struct Record {
    Record() : installed(), shadowed() {
        installed.action = NULL;
        sigemptyset(&installed.mask);
        installed.flags = 0;

        shadowed.sa_handler = SIG_DFL;
        shadowed.sa_flags = 0;
        sigemptyset(&shadowed.sa_mask);
    }

    Record& operator=(const Record& rhs) {
        typed_memcpy(&installed, &rhs.installed);
        typed_memcpy(&shadowed, &rhs.shadowed);
        return *this;
    }

    void init(int sig, const stm_shadow_t& install) {
        // If we've never shadowed this handler, then we want to remember the
        // current handler (otherwise we don't care).
        struct sigaction* old = (isShadowed()) ? NULL : &shadowed;

        // Create a suitable system signal handler. If either install or the
        // existing shadowed handler want to run ONSTACK, then our handler will
        // need to run onstack.
        struct sigaction sa;
        sa.sa_sigaction = do_shadowed_sigaction;
        sa.sa_flags = SA_SIGINFO;
        sa.sa_flags |= install.flags & SA_ONSTACK;
        sa.sa_flags |= shadowed.sa_flags & SA_ONSTACK;

        // Install the handler (remembers existing if necessary).
        if (call_sigaction(sig, &sa, old)) {
            fprintf(stderr, "could not install shadow handler for %i", sig);
            return;
        }

        // Set our installed stm_shadow_t. Need to make sure mask includes sig
        // if we're not deferring.
        installed.action = install.action;
        typed_memcpy(&installed.mask, &install.mask);
        installed.flags = install.flags;
        if (!(install.flags & SA_NODEFER))
            sigaddset(&installed.mask, sig);
    }

    // Emulate signal (update shadowed handler, install compatible handler).
    sighandler_t onSignal(int sig, sighandler_t handler) {
        assert(isShadowed());

        if (call_signal(sig, do_shadowed_signal) == SIG_ERR) {
            fprintf(stderr, "'signal' interposition failed for %i", sig);
            return SIG_ERR;
        }

        sighandler_t old = shadowed.sa_handler;
        shadowed.sa_handler = handler;
        shadowed.sa_flags &= ~SA_SIGINFO;
        return old;
    }

    int onSigaction(int sig, const struct sigaction* act, struct sigaction* o)
    {
        // otherwise, emulate sigaction
        if (o)
            typed_memcpy(o, &shadowed);

        if (act) {
            typed_memcpy(&shadowed, act);

            // make sure we have a compatibe handler installed
            struct sigaction sa;
            sa.sa_sigaction = do_shadowed_sigaction;
            sa.sa_flags = SA_SIGINFO;
            sa.sa_flags |= installed.flags & SA_ONSTACK;
            sa.sa_flags |= shadowed.sa_flags & SA_ONSTACK;
            typed_memcpy(&sa.sa_mask, &installed.mask);
            int r = call_sigaction(sig, &sa, NULL);
            if (r)
                fprintf(stderr, "could not update sigaction, %i", sig);
            return r;
        }

        return 0;
    }

    // Calls the installed action (handles mask).
    void callInstalled(int sig, siginfo_t* info, void* ctx) const
    {
        assert(installed.action && "not masking signal");
        pthread_sigmask(SIG_SETMASK, &installed.mask, NULL);
        installed.action(sig, info, ctx, (shadowed.sa_flags & SA_SIGINFO) ?
                         do_shadowed_sigaction_cont : do_shadowed_signal_cont);
    }

    // Calls the shadowed sigaction (handles mask)
    void callShadowedSigaction(int sig, siginfo_t* info, void* ctx) const {
        assert(shadowed.sa_sigaction && "no shadowed sigaction installed");
        assert(shadowed.sa_flags & SA_SIGINFO && "used signal as sigaction");
        // if (shadowed.sa_sigaction == SIG_IGN)
        //     return;
        // if (shadowed.sa_sigaction == SIG_DFL) {
        //     // TODO: can't just ignore this
        //     return;
        // }
        pthread_sigmask(SIG_SETMASK, &shadowed.sa_mask, NULL);
        shadowed.sa_sigaction(sig, info, ctx);
    }

    // Calls the shadowed signal.
    void callShadowedSignal(int sig) const {
        assert((shadowed.sa_flags & SA_SIGINFO) != SA_SIGINFO && "used sigaction as signal");
        // note no mask set for signal
        if (shadowed.sa_handler == SIG_IGN)
            return;
        if (shadowed.sa_handler == SIG_DFL) {
            fprintf(stderr, "shadowed handler SIG_DFL needs to be emulated for "
                            "%d\n", sig);
            __builtin_exit(1);
        }
        shadowed.sa_handler(sig);
    }

    bool isShadowed() const {
        return installed.action != NULL;
    }

    bool isOneshot() const {
        return shadowed.sa_flags & SA_RESETHAND;
    }

    // Resets the shadowed handler to the default. Need to know sig so that we
    // can set the mask appropriately (these are called via continuations).
    void resetToDefault(int sig) {
        shadowed.sa_handler = SIG_DFL;
        shadowed.sa_flags = 0;
        sigemptyset(&shadowed.sa_mask);
        sigaddset(&shadowed.sa_mask, sig);
    }

  private:
    stm_shadow_t installed;
    struct sigaction shadowed;

    Record(const Record& rhs);
};

// Private inheritence means that only access to the record is through friends'
// get() method which helps to enforce locking protocol. WriteLock and Snapshot
// could be inner classes, but that's probably unneccesary here.
struct VersionedRecord : private Record {
    volatile uintptr_t version;

    VersionedRecord() : Record(), version(0) {
    }

    friend struct WriteLock;
    friend struct Snapshot;
};

// the array of shadowed signals.
static VersionedRecord* ssigs[NSIGS] = {0};

// Read a VersionedRecord consistently---returns a consistent copy of the
// record. Changes made to the record aren't allow because it prevents
// misunderstanding. The record may change asynchronously while the snapshotter
// uses this version, so be careful.
struct Snapshot {
    Snapshot(int sig) : sig_(sig), r_() {
        uintptr_t v;
        do {
            v = ssigs[sig]->version;
            CFENCE;
            r_ = *ssigs[sig];            // full copy record
        } while (v % 2 && v == ssigs[sig]->version);
    }

    const Record& get() const {
        return r_;
    }
  private:
    const int sig_;
    Record r_;
};

// Write a VersionedRecord exclusively---disables all signals while we hold
// it (should only be heald to update a record, do not hold across function
// calls). Just blocking the current sig is a bad idea, because we might
// unblock it in some signal handler and get deadlock that way.
struct WriteLock {
    WriteLock(int sig) : sig_(sig), orig_() {
        // grab the original signal mask so that I can restore it when I
        // release the lock (this could cause a problem if I set a mask while
        // holding a lock, but it doesn't happen in the current code).
        //
        // [ld] for safety I could interpose with pthread_sigmask and
        //      sigprocmask and make sure that these aren't called by a thread
        //      holding a write lock on the record?
        pthread_sigmask(SIG_SETMASK, NULL, &orig_);

        // don't send me a signal while I hold a write lock or I'll have a
        // serious deadlock problem
        sigset_t mask;
        sigfillset(&mask);
        sigaddset(&mask, sig);

        // pardon the unstructured acquire loop, I use it to make the
        // mask-unmask obvious
        uintptr_t v;
      acquire:
        while ((v = ssigs[sig_]->version) % 2)   // read version until even
            spin64();                           // with a cache-friendly spin

        pthread_sigmask(SIG_SETMASK, &mask, NULL); // block all signals
        if (bcasptr(&ssigs[sig]->version, v, v + 1))
            return;                 // I acquired the lock, leave mask in-place

        pthread_sigmask(SIG_SETMASK, &orig_, NULL); // unblock signals for spin
        goto acquire;
    }

    ~WriteLock() {
        // check for overflow here (we don't want to do it on acquire due to
        // the loop-based nature of the v + 1, so we do it here).
        uintptr_t v = ssigs[sig_]->version;
        if (UINTPTR_MAX - v < 2) {
            fprintf(stderr, "ssigs: signal version overflowed, %i", sig_);
            _Exit(-1);
        }

        // release the write lock and restore the signal mask
        ssigs[sig_]->version = v + 1;
        pthread_sigmask(SIG_SETMASK, &orig_, NULL);
    }

    Record& get() {
        // return a reference to the actual record
        return *ssigs[sig_];
    }
  private:
    const int sig_;
    sigset_t orig_;
};
}

/// ---------------------------------------------------------------------------
/// This gets called as a continuation from a shadower when they want to run a
/// shadowed signal handler. Note that this uses a new snapshot of the handler,
/// so it may call a different handler than existed initially.
/// ---------------------------------------------------------------------------
void
do_shadowed_signal_cont(int sig, siginfo_t*, void*)
{
    Snapshot snap(sig);
    const Record& r = snap.get();
    r.callShadowedSignal(sig);
}

/// ---------------------------------------------------------------------------
/// This is installed as the system's sighandler_t for signals that have been
/// registered by the signal system call.
/// ---------------------------------------------------------------------------
void
do_shadowed_signal(int sig)
{
    Snapshot snap(sig);
    const Record& r = snap.get();
    r.callInstalled(sig, NULL, NULL);
}

/// ---------------------------------------------------------------------------
/// This gets called as a continuation from a shadower when they want to run a
/// shadowed signal handler. Note that this uses a new snapshot of the handler,
/// so it may call a different hanlder than existed initially. If the handler
/// is registered with the SA_RESETHAND flag then we need to atomically reset
/// the shadowed handler to SIG_DFL before actually running it.
/// ---------------------------------------------------------------------------
void
do_shadowed_sigaction_cont(int sig, siginfo_t* info, void* ctx)
{
    Snapshot snap(sig);
    Record rec;                         // copy the snapshot
    rec = snap.get();

    if (rec.isOneshot()) {
        // the handler must run only once. We need write access to the record
        // in order to make the change, and we have to check to see if anyone
        // beat us to it. Note lock is scoped and so we release the lock after
        // we update the record, if we find it necessary to update the record.
        WriteLock lock(sig);
        Record& w = lock.get();

        rec = w;                        // copy the locked record

        // If no one beat us to it, reset the shadowed handler to the DFL
        // action for this signal.
        if (w.isOneshot())
            w.resetToDefault(sig);
    }

    rec.callShadowedSigaction(sig, info, ctx);
}

/// ---------------------------------------------------------------------------
/// This is the signal handler we register for shadowed sigaction.
/// ---------------------------------------------------------------------------
void
do_shadowed_sigaction(int sig, siginfo_t* info, void* ctx)
{
    Snapshot snap(sig);
    const Record& r = snap.get();
    r.callInstalled(sig, info, ctx);
}

/// ---------------------------------------------------------------------------
/// External interface to register a shadowing signal.
/// ---------------------------------------------------------------------------
void
stm_shadow_sigaction(int sig, const stm_shadow_t& install)
{
    if (sig < 1 || sig >= NSIGS) {
        fprintf(stderr, "Signal id is out of range: %i\n", sig);
        _Exit(-1);
    }
    else if (ssigs[sig] != NULL) {
        fprintf(stderr, "Warning, reshadowing %i\n", sig);
    }


    ssigs[sig] = new VersionedRecord();

    WriteLock lock(sig);
    Record& w = lock.get();
    w.init(sig, install);
}

/// ---------------------------------------------------------------------------
/// System call interposition.
/// ---------------------------------------------------------------------------
extern "C"
sighandler_t
signal(int sig, sighandler_t handler)
{
    if (sig < 1 || sig >= NSIGS)
        return SIG_ERR;

    // If we're not shadowing this, just use the system handler
    if (!ssigs[sig])
        return call_signal(sig, handler);

    // prevent anyone from racing with me on this signal
    WriteLock lock(sig);
    Record& w = lock.get();
    return w.onSignal(sig, handler);
}

/// ---------------------------------------------------------------------------
/// System call interposition.
/// ---------------------------------------------------------------------------
extern "C"
int
sigaction(int sig, const struct sigaction* act, struct sigaction* out)
{
    if (sig < 1 || sig >= NSIGS)
        return -1;

    // If we're not shadowing this signal, just use the system's handler
    if (!ssigs[sig])
        return call_sigaction(sig, act, out);

    // prevent anyone from racing with me on this update
    WriteLock lock(sig);
    Record& w = lock.get();
    return w.onSigaction(sig, act, out);
}
