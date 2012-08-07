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
 *  This file provides an abstract interface to compiler-specific
 *  declarations.
 *
 *  The declarations in this file are:
 *    NORETURN
 *    NOINLINE
 *    TM_ALIGN
 *    TM_FASTCALL
 */

#ifndef ABSTRACT_COMPILER_HPP__
#define ABSTRACT_COMPILER_HPP__

#include <stdint.h>
#include <limits.h>

/**
 *  For now, we only support gcc...
 */
#ifdef STM_CC_GCC

// indicate that a function does not return
#  define NORETURN        __attribute__((noreturn))

// indicate that a function should not be inlined (use sparingly)
#  define NOINLINE        __attribute__((noinline))

// indicate alignment of variables and/or fields
#  define TM_ALIGN(N)     __attribute__((aligned(N)))

// specify that parameters should be passed in registers (only applies to
// ia32)
#  if defined(STM_CPU_X86) && defined(STM_BITS_32)
#    define TM_FASTCALL     __attribute__((regparm(3)))
#  else
#    define TM_FASTCALL
#  endif

#endif

#endif // ABSTRACT_COMPILER_HPP__
