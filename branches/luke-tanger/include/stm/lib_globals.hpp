/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef LIB_GLOBALS_HPP
#define LIB_GLOBALS_HPP

/**
 *  In this file, we declare functions and variables that need to be visible
 *  to many parts of the STM library.
 */

#include <stm/config.h>
#include <stm/metadata.hpp>

namespace stm
{
  /** A transaction descriptor. */
  struct TxThread;

  /** A convenience typedef. */
  typedef void (*AbortHandler)(TxThread*);

  /**
   *  System initialization and finalization routines. The sys_init call allows
   *  clients to specify a custom abort handler. If no handler is specified
   *  (conflict_abort == NULL) RSTM defaults to setjmp/longjmp control
   *  flow. Clients like the libitm2stm shim may require different, custom
   *  control flow.
   */
  void sys_init(AbortHandler conflict_abort);
  void sys_shutdown();

  /** Thread initialization and finalization. */
  void thread_init();
  void thread_shutdown();

  /**
   *  RSTM's irrevocability interface. This allows clients to query if the
   *  passed thread is already irrevocable, and allows the current thread to
   *  request a change to irrevocable mode.
   *
   *  The change to irrevocable is implemented in an algorithm-specific
   *  fashion. Many of the algorithms can theoretically switch to irrevocable
   *  mode "in-flight" (this usually looks like a partial commit), but we have
   *  only implemented this in a subset of the cases (like NOrec). Other
   *  algorithms are unable to switch to irrevocable mode in-flight because it
   *  could violate their strong publication semantics, like OrecALA.
   *
   *  For those algorithms where we have not implemented in-flight
   *  irrevocability we provide a blanket abort-and-restart-irrevocable
   *  mechanism. This is not ideal because the re-execution may not reach the
   *  "become_irrevoc" call due to seeing a different memory state. As we
   *  implement more in-flight algorithms we can become less dependent on the
   *  abort-and-restart-irrevocable approach, though it will always be
   *  necessary for some algorithms.
   *
   *  become_irrevoc either a) successfully switches in-flight, or b)
   *  aborts. Because aborting is currently a NORETURN interface callers of
   *  this function may assume that, if they return, then the switch was
   *  successful. There is no need to call is_irrevocable before
   *  become_irrevoc.
   */
  bool is_irrevoc(const TxThread&);
  void become_irrevoc();

  /**
   *  Restart the current transaction.
   */
  void restart();

  /**
   *  The manual adaptation interface. The client can get the current algorithm
   *  name, as well as changing the current algorithm (by name), on the fly.
   */
  const char* get_algname();
  void set_policy(const char* phasename);

  /**
   *  Clients, particularly library clients like the libitm2stm shim, need
   *  access to the threads in the system.
   */
  extern pad_word_t  threadcount;           // threads in system
  extern TxThread*   threads[MAX_THREADS];  // all TxThreads
}

#endif // LIB_GLOBALS_HPP
