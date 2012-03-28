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

#define REGISTER_TM_FOR_STANDALONE(NAME, NAMELEN)                       \
    namespace stm                                                       \
    {                                                                   \
        void tm_begin(void*)                                            \
        __attribute__((weak, alias("_ZN" #NAMELEN #NAME "8tm_beginEPv"))); \
        void tm_end()                                                   \
        __attribute__((weak, alias("_ZN" #NAMELEN #NAME "6tm_endEv"))); \
        const char* tm_getalgname()                                     \
        __attribute__((weak, alias("_ZN" #NAMELEN #NAME "13tm_getalgnameEv"))); \
        void* tm_alloc(size_t s)                                        \
        __attribute__((weak, alias("_ZN" #NAMELEN #NAME "8tm_allocEj"))); \
        void tm_free(void* p)                                           \
        __attribute__((weak, alias("_ZN" #NAMELEN #NAME "7tm_freeEPv"))); \
        TM_FASTCALL void* tm_read(void** addr)                          \
        __attribute__((weak, alias("_ZN" #NAMELEN #NAME "7tm_readEPPv"))); \
        TM_FASTCALL void tm_write(void** addr, void* val)               \
        __attribute__((weak, alias("_ZN" #NAMELEN #NAME "8tm_writeEPPvS0_"))); \
        scope_t* rollback(TX*)                                          \
        __attribute__((weak, alias("_ZN" #NAMELEN #NAME "8rollbackEPN3stm2TXE"))); \
    }

#endif // MACROS_HPP__
