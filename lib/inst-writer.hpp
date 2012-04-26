#ifndef RSTM_INST_WRITER_H
#define RSTM_INST_WRITER_H

#include <stdint.h>
#include "tx.hpp"

namespace stm {
  struct BufferedWrite {
      void operator()(void** addr, void* val, TX* tx, uintptr_t mask) const {
          tx->writes.insert(addr, val, mask);
      }
  };

  /**
   *  Used by ITM to log values into the undo log. Supports the _ITM_LOG
   *  interface.
   */
  struct Logger {
      void operator()(void** addr, void* val, TX* tx, uintptr_t mask) const {
          tx->undo_log.insert(addr, val, mask);
      }
  };

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
