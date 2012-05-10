/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_INST_WRITER_H
#define RSTM_INST_WRITER_H

#include <stdint.h>
#include "tx.hpp"

namespace stm {
  template <typename Write>
  struct Writer {
      TX* tx;
      Write write;

      Writer(TX* tx) : tx(tx), write() {
      }

      void operator()(void** address, void* value, uintptr_t mask) const {
          write(address, value, tx, mask);
      }
  };
}

#endif
