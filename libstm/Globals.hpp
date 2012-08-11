/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

/**
 *  Define global variables that we use throughout RSTM
 *
 *  [mfs] We could just weave all of this into txthread.hpp...
 */

#ifndef GLOBALS_HPP__
#define GLOBALS_HPP__

#include "Constants.hpp"
#include "BasicTypes.hpp"
#include "../include/ThreadLocal.hpp"

namespace stm
{
  /*** The type that stores metadata for each thread */
  class TxThread;

  /*** A thread-local pointer to each thread's TxThread object */
  extern THREAD_LOCAL_DECL_TYPE(TxThread*) Self;

  /*** An array of all the threads' TxThread objects */
  extern TxThread* threads[MAX_THREADS];

  /*** The number of TxThread objects created thus far (<= MAX_THREADS) */
  extern pad_word_t threadcount;
}

#endif // GLOBALS_HPP__
