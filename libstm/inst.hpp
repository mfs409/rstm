/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

/***  This file declares the methods that install a new algorithm */

#ifndef INST_HPP__
#define INST_HPP__

#include "../include/ThreadLocal.hpp"
#include "../include/abstract_compiler.hpp"
#include "../include/tlsapi.hpp"
#include "../include/macros.hpp"

namespace stm
{
  /*** forward declare to avoid extra dependencies */
  class TxThread;

  /*** POINTERS TO INSTRUMENTATION */

#ifndef STM_ONESHOT_MODE
  /*** Per-thread commit, read, and write pointers */
  extern THREAD_LOCAL_DECL_TYPE(TM_FASTCALL void(*tmcommit)(TX_LONE_PARAMETER));
  extern THREAD_LOCAL_DECL_TYPE(TM_FASTCALL void*(*tmread)(TX_FIRST_PARAMETER STM_READ_SIG(,)));
  extern THREAD_LOCAL_DECL_TYPE(TM_FASTCALL void(*tmwrite)(TX_FIRST_PARAMETER STM_WRITE_SIG(,,)));
#else
  TM_FASTCALL void  tmcommit(TX_LONE_PARAMETER);
  TM_FASTCALL void* tmread(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void  tmwrite(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
#endif

  /**
   * how to become irrevocable in-flight
   */
  extern bool(*tmirrevoc)(TxThread*);

  /**
   * Some APIs, in particular the itm API at the moment, want to be able
   * to rollback the top level of nesting without actually unwinding the
   * stack. Rollback behavior changes per-implementation (some, such as
   * CGL, can't rollback) so we add it here.
   */
  extern void (*tmrollback)(STM_ROLLBACK_SIG(,,));

  /**
   * The function for aborting a transaction. The "tmabort" function is
   * designed as a configurable function pointer so that an API environment
   * like the itm shim can override the conflict abort behavior of the
   * system. tmabort is configured using sys_init.
   *
   * Some advanced APIs may not want a NORETURN abort function, but the stm
   * library at the moment only handles this option.
   */
  NORETURN void tmabort();

  /**
   *  The read/write/commit instrumentation is reached via per-thread
   *  function pointers, which can be exchanged easily during execution.
   *
   *  The begin function is not a per-thread pointer, and thus we can use
   *  it for synchronization.  This necessitates it being volatile.
   *
   *  The other function pointers can be overwritten by remote threads,
   *  but that the synchronization when using the begin() function avoids
   *  the need for those pointers to be volatile.
   *
   *  NB: read/write/commit pointers were moved out of the descriptor
   *      object to make user code less dependent on this file
   */

  /**
   * The global pointer for starting transactions. The return value should
   * be true if the transaction was started as irrevocable, the caller can
   * use this return to execute completely uninstrumented code if it's
   * available.
   */
  extern void(*volatile tmbegin)(TX_LONE_PARAMETER);

  /*** actually make all threads use the new algorithm */
  void install_algorithm(int new_alg, TxThread* tx);

  /*** make just this thread use a new algorith (use in ctors) */
  void install_algorithm_local(int new_alg);

} // namespace stm

#endif // INST_HPP__
