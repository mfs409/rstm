/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_CHECKPOINT_H
#define RSTM_CHECKPOINT_H

///
/// The compiler ABI for STM in C++ requires that we implement checkpointing
/// manually in asm. This file defines the necessary sizes, offsets, and
/// interface in a platform-dependent manner.
///
/// Start with some ASM macros (this file can be included from .S files).
#if defined(__APPLE__)
#   define ASM_DOT_TYPE(S, T)
#   define ASM_DOT_SIZE(S, T)
#   define ASM_DOT_CFI_STARTPROC
#   define ASM_DOT_CFI_ENDPROC
#   define ASM_DOT_CFI_OFFSET(S, T)
#   define ASM_DOT_CFI_DEF_CFO_OFFSET(S)
#else
#   define ASM_DOT_TYPE(S, T)            .type S, T
#   define ASM_DOT_SIZE(S, T)            .size S, T
#   define ASM_DOT_CFI_STARTPROC         .cfi_startproc
#   define ASM_DOT_CFI_ENDPROC           .cfi_endproc
#   define ASM_DOT_CFI_OFFSET(S, T)      .cfi_offset S, T
#   define ASM_DOT_CFI_DEF_CFO_OFFSET(S) .cfi_def_cfa_offset S
#endif


// Don't include this part from a .S file.
#ifdef __cplusplus
#include <stdint.h>

///
/// Sort out how big a checkpoint we actually need.
///
#if defined(__x86_64__) && defined(__LP64__)    /* x86_64 -m64 */
# define CHECKPOINT_SIZE 8
#elif defined(__x86_64__)                       /* x86 -mx32  */
# define CHECKPOINT_SIZE 8
#elif defined(__i386__)                         /* x86_64 -m32, i?86 */
# define CHECKPOINT_SIZE 6
#elif defined(__sparc__) && defined(__LP64__)   /* sparcv9 -m64 */
#elif defined(__sparc__)                        /* sparcv9 -m32, sparc */
#else
# error "No checkpoint available for your architecture"
#endif

namespace stm {
  /// Like a jmp_buf, a checkpoint_t is just a "big-enough" array.
  typedef void* checkpoint_t[CHECKPOINT_SIZE];

  /// Get a checkpoint to use.
  ///
  /// Note: the __attribute__((regparm(1))) is *important* because it is used in
  /// the custom asm for _ITM_beginTransaction to pass flags correctly.
  checkpoint_t* const pre_checkpoint(const uint32_t flags)
      asm("_rstm_pre_checkpoint") __attribute__((regparm(1)));

  /// Implemented in an architecture-specific asm file, along with
  /// _ITM_beginTransaction. It must not modify the checkpoint because it will
  /// get reused for a conflict abort, where the checkpoint will be reused.
  void restore_checkpoint(const checkpoint_t* const)
      asm("_rstm_restore_checkpoint") __attribute__((noreturn));

  /// Implemented in an algorithm-specific manner. Called from
  /// _ITM_beginTransaction using a sibling call, which is the only reason
  /// that the varargs work without more effort. Must return _ITM_actions to
  /// take, as _ITM_beginTransaction is supposed to do.
  uint32_t post_checkpoint(uint32_t, ...)
      asm("_rstm_post_checkpoint");

  uint32_t post_restart(uint32_t, ...)
      asm("_rstm_post_restart");


  /// Implemented in an algorithm-specific manner. Called from
  /// _ITM_beginTransaction using a sibling call, which is the only reason
  /// that the varargs work without more effort. Must return _ITM_actions to
  /// take, as _ITM_beginTransaction is supposed to do.
  uint32_t post_checkpoint_nested(uint32_t, ...)
      asm("_rstm_post_checkpoint_nested");
}
#endif // __cplusplus

#endif // RSTM_CHECKPOINT_H
