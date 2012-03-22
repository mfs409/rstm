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

/*** Begin sandboxing timer. */
void start_timer();

/*** End sandboxing timer. */
void stop_timer();

/**
 *  We need a way to prevent ourselves from getting interrupted for validation
 *  inside of the stm itself.
 */
class InLib {
  public:
    InLib();
    ~InLib();
};

  void clear_in_lib() asm("stm_sandbox_clear_in_lib") __attribute__((used));
  void set_in_lib() asm("stm_sandbox_set_in_lib") __attribute__((used));
}
}

#endif // LIBSTM_SANDBOXING_HPP
