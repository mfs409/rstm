/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <iostream>
#include "tx.hpp"                               // TX, tm_abort
#include "ldl-utils.hpp"                        // lazy_load_symbol
#include "tmabi.hpp"                            // tm_rollback

namespace {
  /**
   * The gcc implementation of ITM doesn't inject any sort of initialization
   * calls into the binary. We don't want to have to branch in
   * _ITM_beginTransaction, so we deal with this in two ways.
   *
   * - We have the main thread initialize its descriptor in a .ctor
   *   constructor.
   *
   * - We use ldl to interpose pthread_create (used directly, and by libgomp)
   *   and redirect the new thread to our tm_thread_initializer trampoline,
   *   which initializes the new thread's descriptor and then calls the
   *   user-requested function.
   *
   * - We could use custom asm for the tm_thread_initializer to clean up its
   *   presence and then call the user-supplied entry as a sibling call.
   *
   * If we have thread shutdown behavior we'll need to extend this to deal with
   * pthread_exit as well.
   */
  static void __attribute((constructor)) main_thread_init() {
    if (!stm::Self)
        stm::Self = new stm::TX();
  }

  /** A structure to save the user's requesting starting function. */
  struct PackedCreateArgs {
      void* (*start_routine) (void *);
      void*  args;
      PackedCreateArgs(void* (*_start) (void *), void* _args) :
          start_routine(_start), args(_args) {
      }
  };

  /**
   * Our trampoline calls initializes our descriptor, extracts the user's
   * requested entry routine and arguments, deletes the structure so we don't
   * leak the memory, and then calls the user's start routine.
   */
  static void* tm_thread_initializer(void *arg) {
      if (!stm::Self)
          stm::Self = new stm::TX();
      // extract the packed arguments
      PackedCreateArgs* parg = static_cast<PackedCreateArgs*>(arg);
      void* (*start_routine) (void *) = parg->start_routine;
      void* args = parg->args;
      delete parg;
      // TODO: with custom ASM we could fix the arguments and do a sibling call
      //       here, effectively hiding ourselves from the real start_routine.
      return start_routine(args);
  }

  /**
   *  When the transactional system gets shut down, we call this to dump
   *  stats for all threads
   */
  static void __attribute((destructor)) tm_library_shutdown() {
      for (uint32_t i = 0; i < stm::threadcount.val; i++) {
          std::cout << "Thread: "       << stm::threads[i]->id
                    << "; RO Commits: " << stm::threads[i]->commits_ro
                    << "; RW Commits: " << stm::threads[i]->commits_rw
                    << "; Aborts: "     << stm::threads[i]->aborts
                    << "\n";
      }
      CFENCE;
  }
}

/**
 * Interpose pthread_create to start the new thread in our initializer rather
 * than in their requested function.
 */
int
pthread_create(pthread_t *thread, const pthread_attr_t *attr,
               void *(*start_routine) (void *), void *arg) {
    static int (*sys_pthread_create)(pthread_t*,
                                     const pthread_attr_t*,
                                     void* (*) (void *),
                                     void*);
    stm::lazy_load_symbol(sys_pthread_create, "pthread_create");
    // The new'ed object will be deleted in tm_thread_initializer
    return sys_pthread_create(thread, attr, tm_thread_initializer,
                              new PackedCreateArgs(start_routine, arg));
}


namespace stm {
  // for CM
  pad_word_t fcm_timestamp = {0,{0}};
  pad_word_t epochs[MAX_THREADS] = {{0,{0}}};
}
