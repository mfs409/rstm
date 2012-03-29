/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "checkpoint.hpp"
#include "tx.hpp" // TX
#include "libitm.h" // a_save/restoreLiveVariables
using stm::TX;
using stm::Self;
using stm::checkpoint_t;

// [LD] As far as I can tell, there's no general-purpose header that has a
// library API defined.
namespace stm {
  uint32_t tm_begin(uint32_t);
}

checkpoint_t* const
stm::pre_checkpoint(const uint32_t flags) {
    TX& tx = *Self;
    return (++tx.nesting_depth == 1) ? &tx.checkpoint : NULL;
}

// TODO: Can't ignore request for irrevocability from tm_begin.
uint32_t
stm::post_checkpoint(uint32_t flags, ...) {
    return stm::tm_begin(flags) | a_saveLiveVariables;
}

// TODO: can't ignore request for irrevocability from tm_begin.
uint32_t
stm::post_restart(uint32_t flags, ...) {
    return stm::tm_begin(flags) | a_restoreLiveVariables;
}

uint32_t
stm::post_checkpoint_nested(uint32_t flags, ...) {
    return a_runInstrumentedCode;
}
