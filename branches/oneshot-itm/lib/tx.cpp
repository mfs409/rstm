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
#include <setjmp.h> // factor this out into the API?
#include "tx.hpp"

namespace stm
{
  /**
   *  To initialize the thread's TM support, we need only ensure it has a
   *  descriptor.
   */
  void tm_thread_init()
  {
      // multiple inits from one thread do not cause trouble
      if (Self) return;

      // create a TxThread and save it in thread-local storage
      Self = new TX();
  }

  /**
   *  When a thread is done using the TM, we don't need to do anything
   *  special.
   */
  void tm_thread_shutdown() {
  }

  /**
   *  Declaration of the rollback function.
   */
  checkpoint_t* rollback(TX*);

  /**
   *  The default mechanism that libstm uses for an abort. An API environment
   *  may also provide its own abort mechanism (see itm2stm for an example of
   *  how the itm shim does this).
   *
   *  This is ugly because rollback has a configuration-dependent signature.
   */
  void tm_abort(TX* tx) {
      checkpoint_t* checkpoint = rollback(tx);
      tx->nesting_depth = 1;
      restore_checkpoint(checkpoint);
  }

  // for CM
  pad_word_t fcm_timestamp = {0};
  pad_word_t epochs[MAX_THREADS] = {{0}};
}

/**
 *  When the transactional system gets shut down, we call this to dump
 *  stats for all threads
 */
static void __attribute((destructor)) library_shutdown() {
    for (uint32_t i = 0; i < stm::threadcount.val; i++) {
        std::cout << "Thread: "       << stm::threads[i]->id
                  << "; RO Commits: " << stm::threads[i]->commits_ro
                  << "; RW Commits: " << stm::threads[i]->commits_rw
                  << "; Aborts: "     << stm::threads[i]->aborts
                  << "\n";
    }
    CFENCE;
}
