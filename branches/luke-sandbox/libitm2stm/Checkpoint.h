/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef STM_ITM2STM_CHECKPOINT_H
#define STM_ITM2STM_CHECKPOINT_H

#include <stm/config.h> // NORETURN
#include <checkpoint.h> // CHECKPOINT_SIZE

namespace itm2stm {
class Checkpoint {
  public:
    // implemented in arch/$(ARCH)/checkpoint_restore.S
    void restore(uint32_t flags) asm("_stm_itm2stm_checkpoint_restore")
        NORETURN;

    // Returns the address that represents the high value of the protected
    // stack at the time of this call. Currently this means the frame address
    // of the caller.
    //
    // NB: *frame address must be first word!*
    void** stackHigh() const {
        return (void**)checkpoint_[0];
    }

  protected:
    void* checkpoint_[CHECKPOINT_SIZE];
};
} // namespace itm2stm


#endif // STM_ITM2STM_TRANSACTION_H

