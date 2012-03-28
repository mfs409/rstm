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
 * OrecLazyHour is the name for the oreclazy algorithm when instantiated with
 * the "hourglass" CM (see the "Toxic Transactions" paper).  Virtually all of
 * the code is in the oreclazy.hpp file, but we need to instantiate in order
 * to use the "HourglassCM".
 */

#include "OrecLazy.hpp"
#include "cm.hpp"
#include "adaptivity.hpp"
#include "tm_alloc.hpp"

/**
 * Instantiate rollback with the appropriate CM for this TM algorithm. This
 * works because the templated functions in OrecLazy have the right names.
 */
INSTANTIATE_FOR_CM(HourglassCM, 11)

/**
 *  For querying to get the current algorithm name
 */
static const char* tm_getalgname() {
    return "OrecLazyHour";
}

/**
 *  Register the TM for adaptivity and for use as a standalone library
 */
REGISTER_TM_FOR_ADAPTIVITY(OrecLazyHour)
REGISTER_TM_FOR_STANDALONE()
