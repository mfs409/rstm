/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_INST_ALIGNMENT_H
#define RSTM_INST_ALIGNMENT_H

/**
 *  Read and write instrumentation can be optimized if we know details about
 *  the alignment of the accesses. This header encodes alignments in a way that
 *  we can use during instrumentation barrier instantiation (see inst.hpp).
 *
 *  In general, alignment is determined by the target platform that we are
 *  compiling the STM library for, however we also want to be able to override
 *  alignments for research purposes, to quantify the cost of dealing with
 *  unaligned accesses.
 *
 *  This file defines a template, Aligned<T>, that defines a single value that
 *  tells us if the type is guaranteed to be aligned. Then, platform specific
 *  specializations provide the actual types.
 *
 */

namespace stm {
  namespace inst {
    enum Arch {
        x86, x86_64, sparc
    };

#if defined(__i386__)
    static const Arch DEFAULT_ARCH = x86;
#elif defined(__x86_64__) && defined (__LP64__)
    static const Arch DEFAULT_ARCH = x86_64;
#elif defined(__sparc__)
    static const Arch DEFAULT_ARCH = sparc;
#endif

    /**
     *  The Aligned template. By default, accesses are
     *  unaligned---specializations of T, B, and A should define the value enum
     *  to be true.
     */
    template <typename T,
              Arch A = DEFAULT_ARCH,
              size_t B = sizeof(T)>
    struct Aligned {
        enum { value = false };
    };

    /** all sparc accesses are aligned */
    template <typename T, size_t B>
    struct Aligned<T, sparc, B> {
        enum { value = true };
    };

    /** all byte access are aligned */
    template <typename T, Arch A>
    struct Aligned<T, A, 1> {
        enum { value = true };
    };
  }
}
#endif // RSTM_INST_ALIGNMENT_H
