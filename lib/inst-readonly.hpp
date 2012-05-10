/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_INST_READONLY_H
#define RSTM_INST_READONLY_H

#include "tx.hpp"

namespace stm {
  /**
   *  This read-only policy can be used by STM algorithms that would like to
   *  avoid branching in their barriers. This basically means eager, in-place
   *  TMs that don't have RO-specific code. It shouldn't be used by Lazy STMs
   *  that don't have RO-specific code because they want to avoid RAW work for
   *  ReadOnly transactions.
   */
  struct NoReadOnly {
      bool operator()(TX* tx) const {
          return false;
      }
  };

  /**
   *  In general, we can check to see if an STM is readonly by looking at the
   *  size of the writeset.
   */
  struct CheckWritesetForReadOnly {
      bool operator()(TX* tx) {
          return !tx->writes.size();
      }
  };
}

#endif // RSTM_INST_READONLY_H
