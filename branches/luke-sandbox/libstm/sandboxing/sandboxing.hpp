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

namespace stm
{
  /**
   *  Sandboxing TMs need special handling for signals that opaque TMs can
   *  ignore. This should be called from the TM process initializer before any
   *  signals have been registered.
   */
  void install_signal_handlers();
}

#endif // LIBSTM_SANDBOXING_HPP
