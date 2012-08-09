/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 *  License: Modified BSD
 *           Please see the file LICENSE.RSTM for licensing information
 */

#include "Globals.hpp"
#include "inst.hpp"
#include "txthread.hpp"

NORETURN
void stm::tmabort()
{
    stm::TxThread* tx = stm::Self;
    tmrollback(tx);
    tx->nesting_depth = 1;          // no closed nesting yet.
    restore_checkpoint(stm::tmbegin);
}
