#ifndef RSTM_COMMON_INTERPOSITION_H
#define RSTM_COMMON_INTERPOSITION_H

#include <cstdio>                       /* fprintf */
#include <cstdlib>                      /* _Exit */
#include <dlfcn.h>

namespace stm {
/**
 *  Encapsulate the dlsym work required to load a symbol.
 */
template <typename F>
inline void
lazy_load_symbol(F*& f, const char* symbol) {
    // dlsym is idempotent
    if (!f && !(f = reinterpret_cast<F*>(dlsym(RTLD_NEXT, symbol)))) {
        fprintf(stderr, "vsigs: could not load dynamic symbol %s", symbol);
        _Exit(-1);
    }
}
}

#endif // RSTM_COMMON_INTERPOSITION_H
