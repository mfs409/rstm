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
 *  This file establishes a few helpful macros.  Some of these are obvious.
 *  Others are used to simplify some very redundant programming, particularly
 *  with regard to declaring STM functions and abort codes.
 *
 *  [mfs] This is much too visible to applications. Most of it can migrate to
 *        the libstm folder.
 */

/*** Helper Macros for concatenating tokens ***/
#define CAT2(a,b)  a ## b
#define CAT3(a,b,c)  a ## b ## c

/*** Helper Macros for turning a macro token into a string ***/
#define TO_STRING_LITERAL(arg) #arg
#define MAKE_STR(arg) TO_STRING_LITERAL(arg)

/*** max of two vals */
#define MAXIMUM(x,y) (((x)>(y))?(x):(y))

/**
 * C++ iterators can get so ugly without c++0x 'auto'.  These macros are not
 * a good idea, but it makes it much easier to write 79-column code
 */
#define foreach(TYPE, VAR, COLLECTION)                  \
    for (TYPE::iterator VAR = COLLECTION.begin(),       \
         CEND = COLLECTION.end();                       \
         VAR != CEND; ++VAR)

#define foreach_but_last(TYPE, VAR, COLLECTION)         \
    for (TYPE::iterator VAR = COLLECTION.begin(),       \
         CEND = COLLECTION.end();                       \
         (VAR != CEND) && (VAR + 1 != CEND); ++VAR)

#define FOREACH_REVERSE(TYPE, VAR, COLLECTION)          \
    for (TYPE::iterator VAR = COLLECTION.end() - 1,     \
         CAT2(end, __LINE__) = COLLECTION.begin();      \
         VAR >= CAT2(end, __LINE__); --VAR)

/**
 *  When we use compiler-based instrumentation, support for
 *  sub-word-granularity accesses requires the individual STM read/write
 *  functions to take a mask as the third parameter.  The following macros let
 *  us inject a parameter into the function signature as needed for this
 *  purpose.
 */
#if   defined(STM_WS_BYTELOG)
#define STM_MASK(x) , x
#elif defined(STM_WS_WORDLOG)
#define STM_MASK(x)
#else
#error "Either STM_WS_BYTELOG or STM_WS_WORDLOG must be defined"
#endif

#ifdef STM_ABORT_ON_THROW
#   define STM_WHEN_ABORT_ON_THROW(S) S
#else
#   define STM_WHEN_ABORT_ON_THROW(S)
#endif

#if defined(STM_WS_BYTELOG)
#   define STM_READ_SIG(addr, mask)       void** addr, uintptr_t mask
#   define STM_WRITE_SIG(addr, val, mask) void** addr, void* val, uintptr_t mask
#else
#   define STM_READ_SIG(addr, mask)       void** addr
#   define STM_WRITE_SIG(addr, val, mask) void** addr, void* val
#endif

#if defined(STM_ABORT_ON_THROW)
#   define STM_ROLLBACK_SIG(tx, exception, len)  \
    TxThread* tx, void** exception, size_t len
#else
#   define STM_ROLLBACK_SIG(tx, exception, len)  TxThread* tx
#endif

#endif // MACROS_HPP__
