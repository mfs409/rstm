/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef MACROS_HPP__
#define MACROS_HPP__

/**
 * C++ iterators can get so ugly without c++0x 'auto'.  These macros are not
 * a good idea, but it makes it much easier to write 79-column code
 */
#define foreach(TYPE, VAR, COLLECTION)                  \
    for (TYPE::iterator VAR = COLLECTION.begin(),       \
         CEND = COLLECTION.end();                       \
         VAR != CEND; ++VAR)

/**
 * We need to know the mangled names of a bunch of functions, however these
 * names depend on the architecture that we're dealing with.
 */
#if defined(__x86_64__) && defined(__LP64__)
#define TM_BEGIN_SYMBOL "_ZL8tm_beginj"
#define TM_END_SYMBOL "_ZL6tm_endv"
#define TM_GETALGNAME_SYMBOL "_ZL13tm_getalgnamev"
#define TM_ALLOC_SYMBOL "_ZL8tm_allocm"
#define TM_FREE_SYMBOL "_ZL7tm_freePv"
#define TM_READ_SYMBOL "_ZL7tm_readPPv"
#define TM_WRITE_SYMBOL "_ZL8tm_writePPvS_"
#define TM_ROLLBACK_SYMBOL "_ZL8rollbackPN3stm2TXE"

// Some of the tms use explicit template instatiations.
#define SPECIALIZE_TM_ROLLBACK_SYMBOL(CM, NCM)  \
    "_Z8rollbackIN3stm"#NCM#CM"EEPA9_PvPNS0_2TXE"
#define SPECIALIZE_TM_BEGIN_SYMBOL(CM, NCM)     \
    "_Z8tm_beginIN3stm"#NCM#CM"EEjj"
#define SPECIALIZE_TM_END_SYMBOL(CM, NCM)       \
    "_Z6tm_endIN3stm"#NCM#CM"EEvv"
#elif defined(__x86_64__)
#error No TM symbols defined for -mx32 yet, patches welcome.
#elif defined(__i386)
#define TM_BEGIN_SYMBOL "_ZL8tm_beginj"
#define TM_END_SYMBOL "_ZL6tm_endv"
#define TM_GETALGNAME_SYMBOL "_ZL13tm_getalgnamev"
#define TM_ALLOC_SYMBOL "_ZL8tm_allocj"
#define TM_FREE_SYMBOL "_ZL7tm_freePv"
#define TM_READ_SYMBOL "_ZL7tm_readPPv"
#define TM_WRITE_SYMBOL "_ZL8tm_writePPvS_"
#define TM_ROLLBACK_SYMBOL "_ZL8rollbackPN3stm2TXE"

// Some of the tms use explicit template instatiations.
#define SPECIALIZE_TM_ROLLBACK_SYMBOL(CM, NCM)      \
    "_Z8rollbackIN3stm"#NCM#CM"EEPA6_PvPNS0_2TXE"
#define SPECIALIZE_TM_BEGIN_SYMBOL(CM, NCM)     \
    "_Z8tm_beginIN3stm"#NCM#CM"EEjj"
#define SPECIALIZE_TM_END_SYMBOL(CM, NCM)       \
    "_Z6tm_endIN3stm"#NCM#CM"EEvv"
#else
#error No TM symbols defined for your architecture, patches welcome.
#endif

#define REGISTER_TM_FOR_STANDALONE()                                    \
    namespace stm                                                       \
    {                                                                   \
        uint32_t tm_begin(uint32_t)                                     \
        __attribute__((weak, alias(TM_BEGIN_SYMBOL)));                  \
        void tm_end()                                                   \
        __attribute__((weak, alias(TM_END_SYMBOL)));                    \
        const char* tm_getalgname()                                     \
        __attribute__((weak, alias(TM_GETALGNAME_SYMBOL)));             \
        void* tm_alloc(size_t s)                                        \
        __attribute__((weak, alias(TM_ALLOC_SYMBOL)));                  \
        void tm_free(void* p)                                           \
        __attribute__((weak, alias(TM_FREE_SYMBOL)));                   \
        void* tm_read(void** addr)                                      \
        __attribute__((weak, alias(TM_READ_SYMBOL))) TM_FASTCALL;       \
        void tm_write(void** addr, void* val)                           \
        __attribute__((weak, alias(TM_WRITE_SYMBOL))) TM_FASTCALL;      \
        checkpoint_t* rollback(TX*)                                     \
        __attribute__((weak, alias(TM_ROLLBACK_SYMBOL)));               \
    }

#define INSTANTIATE_FOR_CM(CM, NCM)                                     \
    template stm::checkpoint_t* rollback<stm::CM>(stm::TX*);            \
    static stm::checkpoint_t* rollback(TX*)                             \
        __attribute__((alias(SPECIALIZE_TM_ROLLBACK_SYMBOL(CM, NCM)))); \
                                                                        \
    template uint32_t tm_begin<stm::CM>(uint32_t);                      \
    static uint32_t tm_begin(uint32_t)                                  \
        __attribute__((alias(SPECIALIZE_TM_BEGIN_SYMBOL(CM, NCM))));    \
                                                                        \
    template void tm_end<stm::CM>();                                    \
    static void tm_end()                                                \
        __attribute__((alias(SPECIALIZE_TM_END_SYMBOL(CM, NCM))));

#endif // MACROS_HPP__
