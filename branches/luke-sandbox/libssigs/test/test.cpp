#include <cstdio>
#include <csetjmp>
#include <stdint.h>
#include <omp.h>
#include "ssigs/ssigs.h"


__thread sigjmp_buf checkpoint = {{{0}}};
volatile uintptr_t v = 0;
volatile uintptr_t* p = NULL;

extern "C"
int
recurse(int n) {
    sigsetjmp(checkpoint, 1);
    int local[n];
    return local[recurse(n+1)];
}

extern "C"
void
handle_sigsegv(int sig, siginfo_t*, void*);
// {
//     // fprintf(stderr, "got sigsegv");
//     siglongjmp(checkpoint, 1);
// }

extern "C"
bool
wrapper(int sig, siginfo_t* info, void* ctx)
{
    handle_sigsegv(sig, info, ctx);
    return true;
}

int
main(int argc, char* const argv[])
{
    uint8_t as[MINSIGSTKSZ];

    stack_t altstack;
    altstack.ss_sp = &as;
    altstack.ss_flags = 0;
    altstack.ss_size = MINSIGSTKSZ;
    sigaltstack(&altstack, NULL);

    struct sigaction sa;
    sa.sa_sigaction = handle_sigsegv;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);

// #pragma omp parallel
//     {
//         int i = omp_get_thread_num() + 1;
// #pragma omp barrier
        if (sigsetjmp(checkpoint, 1) == 0)
            recurse(1);
    // }
    return 0;
}
