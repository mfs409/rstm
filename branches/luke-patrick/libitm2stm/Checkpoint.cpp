#include "Checkpoint.h"

using namespace itm2stm;

static sigset_t zero;

void init() __attribute__((constructor));
void init() {
    sigemptyset(&zero);
}

bool eq(sigset_t& set) {
    for (unsigned i = 0; i < sizeof(set); ++i) {
        if (((char*)&set)[i] != ((char*)&zero)[i])
            return false;
    }
    return true;
}

void Checkpoint::restore(uint32_t flags) {
    // if we need to restore the mask, do so now.
    if (restoreMask_) {
        pthread_sigmask(SIG_SETMASK, &mask_, NULL);
        restoreMask_ = false;
    }
    restore_asm(flags);
}
