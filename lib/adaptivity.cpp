/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "adaptivity.hpp"

/**
 *  Weak placeholder implementation for libraries that don't use
 *  adaptivity. AdapTM has a strong version of this that gets used when we
 *  include it in the archive.
 */
void __attribute__((weak))
stm::registerTMAlg(int,
                   tm_begin_t, tm_end_t, tm_read_t, tm_write_t,
                   tm_rollback_t, tm_get_alg_name_t, tm_alloc_t, tm_calloc_t,
                   tm_free_t, tm_is_irrevocable_t, tm_become_irrevocable_t)
{ }
