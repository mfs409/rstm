/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

// 5.16  Logging functions

#include "libitm.h"
#include "Transaction.h"
#include "Scope.h"
using namespace itm2stm;

/// _ITM_LB can log arbitrary data. This implements a routine that chunks the
/// passed data into word sized blocks, and logs them all individually.
void
_ITM_LB(_ITM_transaction* td, const void* addr, size_t bytes) {
    void**  address = reinterpret_cast<void**>(const_cast<void*>(addr));
    Scope* scope = td->inner();

    // read and log as many words as we can
    for (size_t i = 0, e = bytes / sizeof(void*); i < e; ++i)
        scope->log(address + i, address[i], sizeof(void*));

    // read all of the remaining bytes and log them
    if (size_t e = bytes % sizeof(void*)) {
        const uint8_t* address8 = reinterpret_cast<const uint8_t*>(addr);
        address8 += bytes - e; // move cursor to the last word

        union {
            uint8_t bytes[sizeof(void*)];
            void* word;
        } buffer = {{0}};

        for (size_t i = 0; i < e; ++i)
            buffer.bytes[i] = address8[i];

        scope->log(reinterpret_cast<void**>(const_cast<uint8_t*>(address8)),
                   buffer.word, e);
    }
}

/// The rest of the log calls have a well-known size at compile time, so we can
/// just directly use the scope's templated log functionality.
#define GENERATE_LOG(TYPE, EXT)                                 \
    void                                                        \
    _ITM_L##EXT(_ITM_transaction* td, const TYPE* address) {    \
        td->inner()->log(address);                              \
    }

GENERATE_LOG(uint8_t, U1)
GENERATE_LOG(uint16_t, U2)
GENERATE_LOG(uint32_t, U4)
GENERATE_LOG(uint64_t, U8)
GENERATE_LOG(float, F)
GENERATE_LOG(double, D)
GENERATE_LOG(long double, E)
GENERATE_LOG(__m64, M64)
GENERATE_LOG(__m128, M128)
#ifdef __AVX__
GENERATE_LOG(__m256, M256)
#endif
GENERATE_LOG(_Complex float, CF)
GENERATE_LOG(_Complex double, CD)
GENERATE_LOG(_Complex long double, CE)
