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
 * OrecLazy is the name for the oreclazy algorithm when instantiated with no
 * CM.  Virtually all of the code is in the oreclazy.hpp file, but we need to
 * instantiate in order to use the "HyperAggressiveCM", which is nops on all
 * transaction boundaries.
 */

#include "OrecLazy.hpp"
#include "cm.hpp"
#include "adaptivity.hpp"
#include "tm_alloc.hpp"

/**
 * Instantiate rollback with the appropriate CM for this TM algorithm. This
 * works because the templated functions in OrecLazy have the right names.
 */
INSTANTIATE_FOR_CM(HyperAggressiveCM, 17)

/**
 *  For querying to get the current algorithm name
 */
static const char* tm_getalgname() {
    return "OrecLazy";
}

/**
 *  Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(OrecLazy)
REGISTER_TM_FOR_STANDALONE()
