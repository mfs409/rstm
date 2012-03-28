/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *   and
 *  Lehigh University Department of Computer Science and Engineering
 *
 *  License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef LIBSTM_INTERPOSITION_HPP
#define LIBSTM_INTERPOSITION_HPP

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
        fprintf(stderr, "could not load dynamic symbol %s", symbol);
        _Exit(-1);
    }
}
}

#endif // LIBSTM_INTERPOSITION_HPP
