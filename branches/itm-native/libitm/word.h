/// -*- C++ -*-
/// Copyright (C) 2012
/// University of Rochester Department of Computer Science
///   and
/// Lehigh University Department of Computer Science and Engineering
///
/// License: Modified BSD
///          Please see the file LICENSE.RSTM for licensing information
///
#ifndef RSTM_WORD_H
#define RSTM_WORD_H

#include <stdint.h>

namespace rstm {
#if defined(__x86_64__)
  typedef uint64_t word_t;
#elif defined(__i386__)
  typedef uint32_t word_t;
#else
# error No word type defined for you platform.
#endif
}

#endif // RSTM_COMMON_H
