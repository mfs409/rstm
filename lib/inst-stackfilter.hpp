/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_INST_STACKFILTER_H
#define RSTM_INST_STACKFILTER_H

#include "tx.hpp"
#include "checkpoint.hpp"

/**
 *  This file is part of the RSTM read/write instrumentation infrastructure. It
 *  describes template policies that implement the StackFilter interface. The
 *  purpose of these templates it to allow the client (stm implementation) to
 *  configure how barrier prefiltering is done.
 *
 *  These policies are hosted by the Instrumentation template in inst.hpp.
 */

namespace stm {
  namespace inst {

    /**
     *  Perform no filtering. This is the policy used by the library
     *  implementation for both read and write instrumentation. Many stm
     *  algorithms use it for their ITM read instrumentation as well.
     */
    struct NoFilter {
        static inline bool filter(void**, TX*) {
            return false;
        }
    };

    /**
     *  A local filter just checks to make sure that the address isn't in the
     *  current stack frame. No one uses this for read or write
     *  instrumentation, but it is used during redo and undo to prevent stack
     *  corruption, if the algorithm's write barrier wasn't filtering using
     *  FullFilter.
     *
     *  This must be inlined for the __builtin_frame_address to work.
     */
    struct LocalFilter {
        static inline bool filter(void** addr, TX*) {
            uintptr_t frame = (uintptr_t)__builtin_frame_address(0);
            assert(frame && "__builtin_frame_address failed");
            return ((frame - (uintptr_t)&frame) > (frame - (uintptr_t)addr));
        }
    };

    /**
     *  Filter accesses to the entire transactional stack, i.e., (in x86 terms)
     *  anything with an address between %esp at the start of the outermost
     *  transaction, and %esp at the time that the filter function is called.
     *
     *  This is used by NOrec in its read barrier, where it can't afford to log
     *  reads to the stack for fear of self-aborts due to aliasing.
     *
     *  We get the top of the stack at the start of the transaction from the
     *  checkpoint that we made at _ITM_beginTransaction. This is done in a
     *  platform dependent manner, and is implemented in checkpoint.hpp.
     *
     *  We get the top of the stack *now* by taking the address of the txtop
     *  variable that we allocated in this frame.
     *
     *    if (txtop - addr > txtop - addr) return true;
     *    else return false;
     *
     *  NB: we're assuming that a T lives either inside the tx stack region, or
     *      outside, but that its consistuent bytes don't overlap the stack
     *      region.
     *  NB: We also rely on unsigned integer underflow creating a large
     *      positive number.
     *  NB: This should be safe to inline because txtop should be in the stack
     *      frame of the caller once it's inlined. [ld] can we guarantee that
     *      it isn't reordered within the same frame?
     */
    struct FullFilter {
        static inline bool filter(void** addr, TX* tx) {
            uintptr_t txtop = (uintptr_t)get_stack_pointer_from_checkpoint(tx);
            return ((txtop - (uintptr_t)&txtop) > (txtop - (uintptr_t)addr));
        }
    };

    /**
     *  This allows prefiltering using the transaction's turbo flag, which is
     *  used in a number of different algorithms. It chains a stack filtering
     *  algorithm, so explicit instantiations should look something like:
     *  'TurboFilter<FullFilter>'.
     */
    template <typename StackFilter>
    struct TurboFilter {
        static inline bool filter(void** addr, TX* tx) {
            return ((tx->turbo) || StackFilter::filter(addr, tx));
        }
    };
  }
}

#endif // RSTM_INST_STACKFILTER_H
