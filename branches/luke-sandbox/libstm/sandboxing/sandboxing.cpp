/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "../sandboxing.hpp"
using stm::TxThread;

/**
 *  Opaque TMs are always valid when this gets called.
 */
bool
stm::default_validate_handler(TxThread*)
{
    return true;
}

void
stm::install_sandboxing_signals()
{
}

void
stm::uninstall_sandboxing_signals()
{
}
