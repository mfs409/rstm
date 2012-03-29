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

#define REGISTER_TM_FOR_STANDALONE()                                    \
    namespace stm                                                       \
    {                                                                   \
        uint32_t tm_begin(uint32_t)                                     \
        __attribute__((weak, alias("_ZL8tm_beginj")));                  \
        void tm_end()                                                   \
        __attribute__((weak, alias("_ZL6tm_endv")));                    \
        const char* tm_getalgname()                                     \
        __attribute__((weak, alias("_ZL13tm_getalgnamev")));            \
        void* tm_alloc(size_t s)                                        \
        __attribute__((weak, alias("_ZL8tm_allocj")));                  \
        void tm_free(void* p)                                           \
        __attribute__((weak, alias("_ZL7tm_freePv")));                  \
        void* tm_read(void** addr)                                      \
        __attribute__((weak, alias("_ZL7tm_readPPv"))) TM_FASTCALL;     \
        void tm_write(void** addr, void* val)                           \
        __attribute__((weak, alias("_ZL8tm_writePPvS_"))) TM_FASTCALL;  \
        checkpoint_t* rollback(TX*)                                     \
        __attribute__((weak, alias("_ZL8rollbackPN3stm2TXE")));         \
    }

#define INSTANTIATE_FOR_CM(CM, NCM)                                     \
    template stm::checkpoint_t* rollback<stm::CM>(stm::TX*);            \
    static stm::checkpoint_t* rollback(TX* tx)                          \
        __attribute__((alias("_Z8rollbackIN3stm"#NCM#CM"EEPA6_PvPNS0_2TXE")));\
                                                                        \
    template uint32_t tm_begin<stm::CM>(uint32_t);                      \
    static uint32_t tm_begin(uint32_t)                                  \
        __attribute__((alias("_Z8tm_beginIN3stm"#NCM#CM"EEjj")));       \
                                                                        \
    template void tm_end<stm::CM>();                                    \
    static void tm_end()                                                \
        __attribute__((alias("_Z6tm_endIN3stm"#NCM#CM"EEvv")));

#endif // MACROS_HPP__
