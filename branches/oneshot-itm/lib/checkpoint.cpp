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
#include "tmabi.hpp"                    // stm::tm_begin
#include "tx.hpp"                       // stm::Self
#include "libitm.h"                     // a_save/restoreLiveVariables
using stm::TX;
using stm::Self;
using stm::tm_begin;

uint32_t
stm::post_restart(uint32_t flags, ...) {
    TX* tx = Self;
    return tm_begin(flags, tx) | a_restoreLiveVariables;
}
