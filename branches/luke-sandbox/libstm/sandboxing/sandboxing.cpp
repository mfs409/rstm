/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <vector>
#include "common/utils.hpp"     // length_of
#include "../sandboxing.hpp"
#include "handlers.hpp"
using stm::TxThread;
using std::vector;

/**
 *  Opaque TMs are always valid when this gets called.
 */
bool
stm::default_validate_handler(TxThread*)
{
    return true;
}

namespace {
  // these are the synchronous signals that we validate.
  const int synchronous[] = {
      SIGSEGV,
      SIGBUS
  };
}

/**
 *  Installs the signal handlers that sandboxing requires.
 */
void
stm::install_sandboxing_signal_handlers()
{
    sigaction_t sa;
    sa.sa_sigaction = validate_synchronous_signal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    for (int i = 0, e = length_of(synchronous); i < e; ++i)
        libstm_internal_sigaction(synchronous[i], &sa);
}

void
stm::uninstall_sandboxing_signal_handlers()
{
    sigaction_t sa;
    sa.sa_handler = SIG_DFL;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    for (int i = 0, e = length_of(synchronous); i < e; ++i)
        libstm_internal_sigaction(synchronous[i], &sa);
}
