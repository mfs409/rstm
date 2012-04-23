/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

/**
 * NOrec with HyperAggressiveCM (no backoff)
 */
#include "NOrec.hpp"
#include "cm.hpp"
#include "adaptivity.hpp"
#include "tm_alloc.hpp"

/**
 * Instantiate rollback with the appropriate CM for this TM algorithm. This
 * works because NOrec.hpp uses the "right" names.
 */
INSTANTIATE_FOR_CM(HyperAggressiveCM, 17)

/**
 *  For querying to get the current algorithm name.
 */
const char* alg_tm_getalgname() {
    return "NOrec";
}

// Register the TM for adaptivity and for use as a standalone library
REGISTER_TM_FOR_ADAPTIVITY(NOrec)

