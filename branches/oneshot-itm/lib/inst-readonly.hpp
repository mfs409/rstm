#ifndef RSTM_INST_READONLY_H
#define RSTM_INST_READONLY_H

#include "tx.hpp"

namespace stm {
  struct CheckWritesetForReadOnly {
      bool operator()(TX* tx) {
          return !tx->writes.size();
      }
  };
}

#endif // RSTM_INST_READONLY_H
