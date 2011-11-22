/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef LIBSTM_SANDBOXING_HPP
#define LIBSTM_SANDBOXING_HPP

#include <csignal>

namespace stm {
namespace sandbox {
/**
 *  Sandboxing TMs need special handling for signals that opaque TMs can
 *  ignore. This should be called from the TM process initializer before any
 *  signals have been registered.
 */
void init_system();

/**
 *  Sandboxing SIGSEGV requires that we have a per-thread alt stack
 *  available. This is called from thread_init to make sure that one is
 *  available.
 */
void init_thread();

/**
 *  Begin sandboxing timer.
 */
void start_timer();

/**
 *  End sandboxing timer.
 */
void stop_timer();

/**
 *  We need a way to prevent ourselves from getting interrupted for validation
 *  inside of the stm itself.
 */
extern __thread int in_lib;
class InLib {
  public:
    InLib();
    ~InLib();
};
}
}

#endif // LIBSTM_SANDBOXING_HPP
