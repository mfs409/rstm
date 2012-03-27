///
/// Copyright (C) 2012
/// University of Rochester Department of Computer Science
///   and
/// Lehigh University Department of Computer Science and Engineering
///
/// License: Modified BSD
///          Please see the file LICENSE.RSTM for licensing information
///
#include "asm.h"

/// NB: we expect these to get inlined with -flto.

void
rstm::nop() {
    __asm__ volatile("nop");
}

void
rstm::spin64() {
    for (int i = 0; i < 64; ++i)
        rstm::nop();
}

