#ifndef RSTM_CHECKPOINT_H
#define RSTM_CHECKPOINT_H

#include <setjmp.h>

namespace stm {
  typedef jmp_buf checkpoint_t;
}

#endif // RSTM_CHECKPOINT_H
