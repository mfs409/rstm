/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_INST_RAW_H
#define RSTM_INST_RAW_H

#include "tx.hpp"

/**
 *  This header define the read-afeter-write algorithms used in the read
 *  instrumentation (inst.hpp) as RAW policies. The usage expected is as
 *  follows.
 */

namespace stm {
  namespace inst {
    /**
     *  The NoRAW policy does not perform a read-after-write check, and is
     *  suitable for in-place accesses.
     */
    struct NoRAW {
        static bool hit(void**, void*&, TX*, uintptr_t) {
            return false;
        }

        static void merge(void* val, void*& storage) {
            storage = val;
        }
    };

    /**
     *  The wordlog read-after-write template simply checks the writelog for a
     *  hit. In this context, hits can't be partial (we've either written the
     *  whole word, or we haven't) so we don't need to do anything special to
     *  merge.
     */
    struct WordlogRAW {
        bool hit(void** addr, void*& storage, TX* tx, uintptr_t) {
            return (tx->writes.size()) ? tx->writes.find(addr, storage) : false;
        }

        void merge(void* val, void*& storage) {
            storage = val;
        }
    };

    /**
     *  The bytelog read-after-write template needs to keep track of the mask,
     *  and the storage location.
     */
    struct BytelogRAW {
        uintptr_t missing_;

        bool hit(void** addr, void*& storage, TX* tx, uintptr_t mask) {
            return (tx->writes.size()) ?
            !(missing_ = mask & ~tx->writes.find(addr, storage)) : false;
        }

        void merge(void* val, void*& storage) {
            storage = (void*)((uintptr_t)storage & ~missing_);
            storage = (void*)((uintptr_t)storage | ((uintptr_t)val & missing_));
        }
    };
  }
}

#endif // RSTM_INST_RAW_H
