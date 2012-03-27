/// -*- C++ -*-
/// Copyright (C) 2012
/// University of Rochester Department of Computer Science
///   and
/// Lehigh University Department of Computer Science and Engineering
///
/// License: Modified BSD
///          Please see the file LICENSE.RSTM for licensing information
///
#ifndef RSTM_ASM_H
#define RSTM_ASM_H

///
/// Our ultimate goal is to eliminate this file. Unfortunately, compilers don't
/// appear to have any __builtin_nop intrinsics, so at this point we need to
/// provide at least asm for that.
///

namespace rstm {
  void nop();
  void spin64();
}

#endif // RSTM_ASM_H
