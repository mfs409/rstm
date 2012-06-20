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

/**
 *  The compiler ABI for STM in C++ requires that we implement checkpointing
 *  manually in asm. This file defines the necessary sizes, offsets, and
 *  interface in a platform-dependent manner.
 *
 *  Start with some ASM macros (this file can be included from .S files).
 */
#if defined(__APPLE__) || defined(__sun__)
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
#include "adaptivity.hpp"               // tm_begin_t

/** Sort out how big a checkpoint we actually need, based on the arch. */
#if defined(__x86_64__) && defined(__LP64__)    /* x86_64 -m64 */
# define CHECKPOINT_SIZE 9
#elif defined(__x86_64__)                       /* x86 -mx32  */
#error No checkpoint infrastructure for -mx32 at the moment. Patches welcome.
#elif defined(__i386__)                         /* x86_64 -m32, i?86 */
# define CHECKPOINT_SIZE 7
#elif defined(__sparc__) && defined(__LP64__)   /* sparcv9 -m64 */
#error No checkpoint infrastructure for sparcv9 at the moment. Patches welcome.
#elif defined(__sparc__)                        /* sparcv9 -m32, sparc */
#error No checkpoint infrastructure for sparc at the moment. Patches welcome.
#else
# error "No checkpoint available for your architecture"
#endif

namespace stm {
  /** Like a jmp_buf, a checkpoint_t is just a "big-enough" array of words. */
  typedef void* checkpoint_t[CHECKPOINT_SIZE];
  static const int CHECKPOINT_SP_OFFSET = 1;

  struct TX;

  /**
   *  Called to restore the checkpoint. The continuation pointer is called
   *  after we've restored the state to that of the time of the checkpoint, and
   *  allows us to control the post-checkpoint behavior from the calling site
   *  (i.e., _ITM_abortTransaction).
   */
  void restore_checkpoint(uint32_t (*)(uint32_t, TX*) TM_FASTCALL, const TX* const)
      asm("_rstm_restore_checkpoint") NORETURN;
}
#endif // __cplusplus

#endif // RSTM_CHECKPOINT_H
