/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_TMABI_WEAK_HPP
#define RSTM_TMABI_WEAK_HPP

#include "platform.hpp"                 // TM_FASTCALL
#include "libitm.h"

/**
 *  This header should be included by TM implementations that wish to be
 *  compatible with adaptivity. It defines the /actual/ symbols as
 *  non-namespaced static symbols (though they could just as easily be statics
 *  in a non-stm namespace), and then defines the weak aliases for
 *  compatibility with the tmabi.hpp interface.
 *
 *  IMPLEMENTORS: To implement an adaptable TM, include this file and implement
 *                the static functions declared at the end of the file.
 */

/**
 * We need to know the mangled names of the static interface, however these
 * names depend on the architecture that we're dealing with. In gcc we would do
 * this using the asm() labeling extension, but this is unsupported by our
 * version of sun CC.
 *
 * [ld] there /has/ to be a better way to deal with this. sun CC suports
 *      '#prama weak symbol = symbol' which uses source names instead of symbol
 *      strings, and the same pragma is available in gcc, but I don't know if
 *      it accepts symbols. I also don't think that the #pragma will support
 *      template specializations, so there might just be no "good" way to do
 *      this without gcc's asm() extension.
 */
#if defined(__x86_64__) && defined(__LP64__)
#define TM_BEGIN_SYMBOL "_ZL12alg_tm_beginjPN3stm2TXE"
#define TM_END_SYMBOL "_ZL10alg_tm_endv"
#define TM_GETALGNAME_SYMBOL "_ZL17alg_tm_getalgnamev"
#define TM_ALLOC_SYMBOL "_ZL12alg_tm_allocm"
#define TM_CALLOC_SYMBOL "_ZL13alg_tm_callocmm"
#define TM_FREE_SYMBOL "_ZL11alg_tm_freePv"
#define TM_READ_SYMBOL "_ZL11alg_tm_readPPv"
#define TM_WRITE_SYMBOL "_ZL12alg_tm_writePPvS_"
#define TM_ROLLBACK_SYMBOL "_ZL15alg_tm_rollbackPN3stm2TXE"
#define TM_IS_IRREVOCABLE_SYMBOL "_ZL21alg_tm_is_irrevocablePN3stm2TXE"
#define TM_BECOME_IRREVOCABLE_SYMBOL "_ZL25alg_tm_become_irrevocable21_ITM_transactionState"

// Some of the tms use explicit template instatiations.
#define SPECIALIZE_TM_ROLLBACK_SYMBOL(CM, NCM) "_Z15alg_tm_rollbackIN3stm"#NCM#CM"EEvPNS0_2TXE"
#define SPECIALIZE_TM_BEGIN_SYMBOL(CM, NCM) "_Z12alg_tm_beginIN3stm"#NCM#CM"EEjjPNS0_2TXE"
#define SPECIALIZE_TM_END_SYMBOL(CM, NCM) "_Z10alg_tm_endIN3stm"#NCM#CM"EEvv"

#elif defined(__x86_64__)
#error No TM symbols defined for -mx32 yet, patches welcome.
#elif defined(__i386)
#define TM_BEGIN_SYMBOL "_ZL12alg_tm_beginjPN3stm2TXE"
#define TM_END_SYMBOL "_ZL10alg_tm_endv"
#define TM_GETALGNAME_SYMBOL "_ZL17alg_tm_getalgnamev"
#define TM_ALLOC_SYMBOL "_ZL12alg_tm_allocj"
#define TM_CALLOC_SYMBOL "_ZL13alg_tm_callocjj"
#define TM_FREE_SYMBOL "_ZL11alg_tm_freePv"
#define TM_READ_SYMBOL "_ZL11alg_tm_readPPv"
#define TM_WRITE_SYMBOL "_ZL12alg_tm_writePPvS_"
#define TM_ROLLBACK_SYMBOL "_ZL15alg_tm_rollbackPN3stm2TXE"
#define TM_IS_IRREVOCABLE_SYMBOL "_ZL21alg_tm_is_irrevocablePN3stm2TXE"
#define TM_BECOME_IRREVOCABLE_SYMBOL "_ZL25alg_tm_become_irrevocable21_ITM_transactionState"

// Some of the tms use explicit template instatiations.
#define SPECIALIZE_TM_ROLLBACK_SYMBOL(CM, NCM) "_Z15alg_tm_rollbackIN3stm"#NCM#CM"EEvPNS0_2TXE"
#define SPECIALIZE_TM_BEGIN_SYMBOL(CM, NCM) "_Z12alg_tm_beginIN3stm"#NCM#CM"EEjjPNS0_2TXE"
#define SPECIALIZE_TM_END_SYMBOL(CM, NCM) "_Z10alg_tm_endIN3stm"#NCM#CM"EEvv"

#else
#error No TM symbols defined for your architecture, patches welcome.
#endif

// [ld] This is here because I'm not sure where a better place to put it is.
#define INSTANTIATE_FOR_CM(CM, NCM)                                     \
    template void alg_tm_rollback<stm::CM>(stm::TX*);                   \
    static void alg_tm_rollback(TX*)                                    \
        __attribute__((alias(SPECIALIZE_TM_ROLLBACK_SYMBOL(CM, NCM)))); \
                                                                        \
    template uint32_t alg_tm_begin<stm::CM>(uint32_t, stm::TX*);        \
    static uint32_t alg_tm_begin(uint32_t, stm::TX*)                    \
        __attribute__((alias(SPECIALIZE_TM_BEGIN_SYMBOL(CM, NCM))))     \
        TM_FASTCALL;                                                    \
                                                                        \
    template void alg_tm_end<stm::CM>();                                \
    static void alg_tm_end()                                            \
        __attribute__((alias(SPECIALIZE_TM_END_SYMBOL(CM, NCM))));


/**
 *  The following weak aliases mean that, if we link to a library that doesn't
 *  include AdapTM, the alg-specific symbols will be linked directly.
 */
namespace stm {
  struct TX;

  uint32_t tm_begin(uint32_t, TX*)
      __attribute__((weak, alias(TM_BEGIN_SYMBOL))) TM_FASTCALL;
  const char* tm_getalgname()
      __attribute__((weak, alias(TM_GETALGNAME_SYMBOL)));
  void* tm_alloc(size_t)
      __attribute__((malloc, weak, alias(TM_ALLOC_SYMBOL)));
  void tm_free(void*)
      __attribute__((weak, alias(TM_FREE_SYMBOL)));
  void* tm_read(void**)
      __attribute__((weak, alias(TM_READ_SYMBOL))) TM_FASTCALL;
  void tm_write(void**, void*)
      __attribute__((weak, alias(TM_WRITE_SYMBOL))) TM_FASTCALL;
  void tm_rollback(TX*)
      __attribute__((weak, alias(TM_ROLLBACK_SYMBOL)));
  bool tm_is_irrevocable(TX*)
      __attribute__((weak, alias(TM_IS_IRREVOCABLE_SYMBOL)));
}

void _ITM_commitTransaction() ITM_REGPARM
    __attribute__((weak, alias(TM_END_SYMBOL)));

void _ITM_changeTransactionMode(_ITM_transactionState) ITM_REGPARM
    __attribute__((weak, alias(TM_BECOME_IRREVOCABLE_SYMBOL)));

void* _ITM_malloc(size_t)
    __attribute__((malloc, weak, alias(TM_ALLOC_SYMBOL))) GCC_ITM_PURE;

void* _ITM_calloc(size_t, size_t)
    __attribute__((malloc, weak, alias(TM_CALLOC_SYMBOL))) GCC_ITM_PURE;

void _ITM_free(void*)
    __attribute__((weak, alias(TM_FREE_SYMBOL))) GCC_ITM_PURE;


/**
 *  These are the algorithm specific functions that need to be implemented for
 *  this header to work correctly. They're also used in the registration macro
 *  in adaptivity.hpp.
 */
static uint32_t    alg_tm_begin(uint32_t, stm::TX*) TM_FASTCALL;
static void        alg_tm_end();
static const char* alg_tm_getalgname();
static void*       alg_tm_alloc(size_t) __attribute__((malloc));
static void*       alg_tm_calloc(size_t, size_t) __attribute__((malloc));
static void        alg_tm_free(void*);
static void*       alg_tm_read(void**) TM_FASTCALL;
static void        alg_tm_write(void**, void*) TM_FASTCALL;
static void        alg_tm_rollback(stm::TX*);
static bool        alg_tm_is_irrevocable(stm::TX*);
static void        alg_tm_become_irrevocable(_ITM_transactionState) ITM_REGPARM;

#endif // RSTM_TMABI_WEAK_HPP

