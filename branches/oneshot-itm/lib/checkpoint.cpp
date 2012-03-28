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
#include "libitm.h" // a_runInstrumentedCode | a_saveLiveVariables
using stm::TX;
using stm::Self;
using rstm::checkpoint_t;

checkpoint_t* const
rstm::pre_checkpoint(const uint32_t flags) {
    TX& tx = *Self;
}

uint32_t
rstm::post_checkpoint(uint32_t flags, ...) {
    return a_runInstrumentedCode | a_saveLiveVariables;
}

uint32_t
rstm::post_checkpoint_nested(uint32_t flags, ...) {
    return a_runInstrumentedCode;
}
