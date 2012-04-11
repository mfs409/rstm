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
#include "inst.hpp"                     // the generic R/W instrumentation
#include "inst-stackfilter.hpp"         // FullFilter
#include "inst-raw.hpp"

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

using namespace stm::inst;

/**
 *  Instantiate our read template for all of the read types, and add weak
 *  aliases for the LIBITM symbols to them.
 *
 *  TODO: We can't make weak aliases without mangling the symbol names, but
 *        this is non-trivial for the instrumentation templates. For now, we
 *        just inline the read templates into weak versions of the library. We
 *        could use gcc's asm() exetension to instantiate the template with a
 *        reasonable name...?
 */
#define RSTM_LIBITM_READ(SYMBOL, CALLING_CONVENTION, TYPE)              \
    extern TYPE CALLING_CONVENTION __attribute__((weak)) SYMBOL(TYPE* addr) {  \
        return read<TYPE, FullFilter, WordlogRAW, false>(addr);         \
    }

#include "libitm-dtfns.def"

#undef RSTM_LIBITM_READ
