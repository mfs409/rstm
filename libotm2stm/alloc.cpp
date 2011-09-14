/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <new>
#include <stdlib.h>

/**
 *  We need a wrapper for calls to operator new on the undecorated path.
 *  This will do it, but I'm not sure if it really works yet.
 */
[[transaction_safe]]
void* operator new(size_t size)  throw()
{
    // [mfs] Do we need to initialize the library before calling malloc?
    //       Does this call become a STM_TranMalloc?  STM_TranMalloc thinks
    //       it is getting a valid descriptor, and we don't want
    //       GetMyTransId() to pay the overhead, so if this becomes a
    //       STM_TranMalloc, then it should first have a waiver block to do a
    //       conditional thread_init()
    return malloc(size);
}

/**
 *  Likewise, we need a wrapper for calls to operator delete on the
 *  undecorated path.  This will do it, but I'm not sure if it really works
 *  yet.
 */
[[transaction_safe]]
void operator delete(void* ptr) throw()
{
    // [mfs] see the comments in operator new... we probably need to
    // initialize a thread here
    free(ptr);
}
