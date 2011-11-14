/* =============================================================================
 *
 * Copyright (C) Stanford University, 2010.  All Rights Reserved.
 * Author: Sungpack Hong and Jared Casper
 *
 * =============================================================================
 */

#ifndef _STAMP_API_FOR_TL2_
#define _STAMP_API_FOR_TL2_

// include from tl2 release package
#include "tl2.h"    
#include "util.h"


#define STM_THREAD_T                    Thread
#define STM_SELF                        Self
#define STM_RO_FLAG                     ROFlag

#define STM_MALLOC(size)                TxAlloc(STM_SELF, size)
#define STM_FREE(ptr)                   TxFree(STM_SELF, ptr)

#  define malloc(size)                  tmalloc_reserve(size)
#  define calloc(n, size)               ({ \
                                            size_t numByte = (n) * (size); \
                                            void* ptr = tmalloc_reserve(numByte); \
                                            if (ptr) { \
                                                memset(ptr, 0, numByte); \
                                            } \
                                            ptr; \
                                        })
#  define realloc(ptr, size)            tmalloc_reserveAgain(ptr, size)
#  define free(ptr)                     tmalloc_release(ptr)

#  include <setjmp.h>
#  define STM_JMPBUF_T                  sigjmp_buf
#  define STM_JMPBUF                    buf

#define STM_VALID()                     (1)
#define STM_RESTART()                   TxAbort(STM_SELF)

#define STM_STARTUP()                   TxOnce()
#define STM_SHUTDOWN()                  TxShutdown()

#define STM_NEW_THREAD()                TxNewThread()
#define STM_INIT_THREAD(t, id)          TxInitThread(t, id)
#define STM_FREE_THREAD(t)              TxFreeThread(t)

#  define STM_BEGIN(isReadOnly)         do { \
                                            STM_JMPBUF_T STM_JMPBUF; \
                                            int STM_RO_FLAG = isReadOnly; \
                                            sigsetjmp(STM_JMPBUF, 1); \
                                            TxStart(STM_SELF, &STM_JMPBUF, &STM_RO_FLAG); \
                                        } while (0) /* enforce comma */

#define STM_BEGIN_RD()                  STM_BEGIN(1)
#define STM_BEGIN_WR()                  STM_BEGIN(0)
#define STM_END()                       do {\
                                            TxCommit(STM_SELF); \
                                        } while(0)

typedef volatile intptr_t               vintp;

#define STM_READ(var)                   TxLoad(STM_SELF, (vintp*)(void*)&(var))
#define STM_READ_F(var)                 IP2F(TxLoad(STM_SELF, \
                                                    (vintp*)FP2IPP(&(var))))
#define STM_READ_P(var)                 IP2VP(TxLoad(STM_SELF, \
                                                     (vintp*)(void*)&(var)))

#define STM_WRITE(var, val)             TxStore(STM_SELF, \
                                                (vintp*)(void*)&(var), \
                                                (intptr_t)(val))
#define STM_WRITE_F(var, val)           TxStore(STM_SELF, \
                                                (vintp*)FP2IPP(&(var)), \
                                                F2IP(val))
#define STM_WRITE_P(var, val)           TxStore(STM_SELF, \
                                                (vintp*)(void*)&(var), \
                                                VP2IP(val))

#define STM_LOCAL_WRITE(var, val)       ({var = val; var;})
#define STM_LOCAL_WRITE_F(var, val)     ({var = val; var;})
#define STM_LOCAL_WRITE_P(var, val)     ({var = val; var;})

#define TM_ARG              STM_SELF,
#define TM_ARGDECL          STM_THREAD_T* TM_ARG


# define TM_STARTUP()       STM_STARTUP()
# define TM_SHUTDOWN()      STM_SHUTDOWN()
# define TM_THREAD_ENTER()  STM_THREAD_T* STM_SELF = STM_NEW_THREAD(); \
                            STM_INIT_THREAD(STM_SELF, tid)
# define TM_THREAD_EXIT()   STM_FREE_THREAD(STM_SELF)
# define TM_BEGIN()         STM_BEGIN_WR()
# define TM_END()           STM_END()

#endif
