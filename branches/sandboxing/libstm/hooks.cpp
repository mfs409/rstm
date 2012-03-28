/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <dlfcn.h>
#include <stm/WBMMPolicy.hpp>

int (*_munmap)(void *, size_t) = NULL;

inline void tx_fence()
{
    uint32_t count = stm::threadcount.val;
    for (uint32_t i = 0; i < count; i++) {
        // read the per-thread counter
        uint32_t v_old = stm::trans_nums[i].val;
        // we have to wait if the counter is odd
        if ((v_old % 2) == 1) {
            uint32_t v_new = v_old;
            // wait until counter changes
            while (v_new == v_old)
                v_new = stm::trans_nums[i].val;
        }
    }
}

int munmap(void * addr, size_t len)
{
    // look for the original lib function
    if (_munmap == NULL)
        _munmap = (int (*)(void *, size_t))dlsym(RTLD_NEXT, "munmap");

    // wait for quietscence
    tx_fence();

    // invoke the original lib function
    return _munmap(addr, len);
}
