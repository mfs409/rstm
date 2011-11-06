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
  struct TxThread;

  /**
   *  Our TMs are generally opaque, which means that they are always valid when
   *  returning from tmread. These TMs use this default tmvalidate handler so
   *  that that can be adapted into an otherwise sandboxed setting.
   */
  bool default_validate_handler(TxThread*);

  /**
   *  Sandboxing TMs need special handling for signals that opaque TMs can
   *  ignore. These two calls install and uninstall the sandboxing signal
   *  handlers. Ultimately, it might make sense to have this be thread-specific
   *  (there isn't really any reason that compatible opaque and sandboxing TMs
   *  can't execute concurrently), which we'd to by installing handlers that
   *  dispatch to thread-specific routines.
   */
  void install_sandboxing_signals();
  void uninstall_sandboxing_signals();
}

#endif // LIBSTM_SANDBOXING_HPP
