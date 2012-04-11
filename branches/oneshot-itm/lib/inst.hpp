/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_INST_H
#define RSTM_INST_H

#include <stdint.h>
#include "tx.hpp"
#include "inst-alignment.hpp"

/**
 *  The intrinsic read and write barriers are implemented by the TM.
 */
static void* alg_tm_read_aligned_word(void** addr, stm::TX*);
static void alg_tm_write_aligned_word(void** addr, void* val, stm::TX*);

namespace stm {
  namespace inst {
    /**
     *  This template gets specialized to tell us how many word-size accesses
     *  need to be done in order to satisfy an access to the given type. This
     *  is a maximum size, it might be 1 word too big when the type isn't
     *  guaranteed to be aligned, but the address is actually aligned. We
     *  account for this in the loop that actually performs the read.
     */
    template <typename T,
              bool ALIGNED,
              size_t M = sizeof(T) % sizeof(void*)>
    struct Buffer;

    /** Aligned subword types only need 1 word. */
    template <typename T, size_t M>
    struct Buffer<T, true, M> {
        enum { WORDS = 1 };
    };

    /** Possibly unaligned subword types may need 2 words. */
    template <typename T, size_t M>
    struct Buffer<T, false, M> {
        enum { WORDS = 2 };
    };

    /**
     *  Aligned word and multiword types
     *
     *  NB: we assume that this will not be instantiated with multiword types
     *      that aren't multiples of the size of a word. We could check this
     *      with static-asserts, if we had them.
     */
    template <typename T>
    struct Buffer<T, true, 0> {
        enum { WORDS = sizeof(T)/sizeof(void*) };
    };

    /**
     *  Possibly unaligned word and multiword types may need an extra word.
     *
     *  NB: we assume that this will not be instantiated with multiword types
     *      that aren't multiples of the size of a word. We could check this
     *      with static-asserts, if we had them.
     */
    template <typename T>
    struct Buffer<T, false, 0> {
        enum { WORDS = sizeof(T)/sizeof(void*) + 1 };
    };

    /**
     *  Addresses for everything other than aligned word and multiword accesses
     *  may need to be adjusted to a word boundary.
     */
    template <typename T,
              bool ALIGNED,
              size_t M = sizeof(T) % sizeof(void*)>
    struct Base {
        static inline void** of(T* addr) {
            const uintptr_t MASK = ~static_cast<uintptr_t>(sizeof(void*) - 1);
            const uintptr_t base = reinterpret_cast<uintptr_t>(addr) & MASK;
            return reinterpret_cast<void**>(base);
        }
    };

    /**
     *  Aligned words (or multiples of words) don't need to be adjusted.
     */
    template <typename T>
    struct Base<T, true, 0> {
        static inline void** of(T* addr) {
            return reinterpret_cast<void**>(addr);
        }
    };

    /**
     *  We need to know the offset within a word for verything other than
     *  aligned words or multiword accesses.
     */
    template <typename T,
              bool ALIGNED,
              size_t M = sizeof(T) % sizeof(void*)>
    struct Offset {
        static inline size_t of(const T* const addr) {
            const uintptr_t MASK = static_cast<uintptr_t>(sizeof(void*) - 1);
            const uintptr_t offset = reinterpret_cast<uintptr_t>(addr) & MASK;
            return static_cast<size_t>(offset);
        }
    };

    /**
     *  Aligned word and multiword accessed have a known offset of 0.
     */
    template <typename T>
    struct Offset<T, true, 0> {
        static inline size_t of(T* addr) {
            return 0;
        }
    };

    /**
     *  Whenever we need to perform a transactional load or store we need a
     *  mask that has 0xFF in all of the bytes that we are intersted in. This
     *  computes a mask given an [i, j) range, where 0 <= i < j <=
     *  sizeof(void*).
     *
     *  NB: When the parameters are compile-time constants we expect this to
     *    become a simple constant in the binary when compiled with
     *    optimizations.
     */
    static inline uintptr_t
    make_mask(size_t i, size_t j) {
        // assert(0 <= i && i < j && j <= sizeof(void*) && "range is incorrect")
        uintptr_t mask = ~(uintptr_t)0;
        mask = mask >> (8 * (sizeof(void*) - j + i)); // shift 0s to the top
        mask = mask << (8 * i);                       // shift 0s into the bottom
        return mask;
    }

    static inline size_t min(size_t lhs, size_t rhs) {
        return (lhs < rhs) ? lhs : rhs;
    };

    /**
     *  This is a generic read instrumentation routine, that uses the client's
     *  stack filter and read-after-write mechanism. The routine is completely
     *  generic, but we've used as much compile-time logic as we can. We expect
     *  that any modern compiler will be able to eliminate branches where
     *  they're not needed.
     */
    template <typename T,
              class StackFilter,
              class RAW,
              bool ForceAligned>
    static inline T read(T* addr) {
        TX* tx = Self;

        // see if this is a read from the stack
        if (StackFilter::filter((void**)addr, tx))
            return *addr;

        const bool ALIGNED = Aligned<T, ForceAligned>::value;

        // using Buffer<T>::WORDS;
        enum { W = Buffer<T, ALIGNED>::WORDS };

        // the bytes union is used to deal with unaligned and/or subword data.
        union {
            void* words[W];
            uint8_t bytes[W * sizeof(void*)];
        };

        // Some read-after-write algorithms need local storage.
        RAW raw;

        // adjust the base pointer for possibly non-word aligned accesses
        void** base = Base<T, ALIGNED>::of(addr);

        // compute an offset for this address
        const size_t off = Offset<T, ALIGNED>::of(addr);

        // deal with the first word, there's always at least one
        uintptr_t mask = make_mask(off, min(sizeof(void*), off + sizeof(T)));
        if (!raw.hit(base, words[0], tx, mask))
            raw.merge(alg_tm_read_aligned_word(base, tx), words[0]);

        // deal with any middle words
        mask = make_mask(0, sizeof(void*));
        for (int i = 1, e = W - 1; i < e; ++i) {
            if (raw.hit(base + i, words[i], tx, mask))
                continue;
            raw.merge(alg_tm_read_aligned_word(base + i, tx), words[i]);
        }

        // deal with the last word, the second check is just offset, because we
        // already checked to see if an access overflowed in dealing with the
        // first word.
        if (W > 1 && off) {
            mask = make_mask(0, off);
            if (!raw.hit(base + W, words[W], tx, mask))
                raw.merge(alg_tm_read_aligned_word(base + W, tx), words[W]);
        }

        return *reinterpret_cast<T*>(bytes + off);
    }
  }
}

#define SPECIALIZE_INST_READ_SYMBOL(T, SF, RAW)

#endif // RSTM_INST_H
