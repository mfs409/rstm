///
/// Copyright (C) 2012
/// University of Rochester Department of Computer Science
///   and
/// Lehigh University Department of Computer Science and Engineering
///
/// License: Modified BSD
///          Please see the file LICENSE.RSTM for licensing information
///
#include <cstddef>                              // NULL
#include "locks.h"
#include "asm.h"                                // nop()

namespace {
  static void backoff(int& b) {
      // Tune backoff parameters
      //
      // NB: At some point (probably mid 2010), these values were experimentally
      //     verified to provide good performance for some workload using TATAS
      //     locks.  Whether they are good values anymore is an open question.
#if defined(__sparc__)
      static const int MAX_TATAS_BACKOFF = 4096;
#else
      static const int MAX_TATAS_BACKOFF = 524288;
#endif

      for (int i = b; i; --i)
          rstm::nop();
      if (b < MAX_TATAS_BACKOFF)
          b <<= 1;
  }

  /// Full test-and-test-and-set with exponential backoff.
  static int acquire_slowpath(rstm::tatas_lock_t& lock) {
      int b = 64;
      do {
          backoff(b);
      } while (__sync_lock_test_and_set(&lock, 1));
      return b;
  }
}

int
rstm::acquire(rstm::tatas_lock_t& lock) {
    return (__sync_lock_test_and_set(&lock, 1)) ? acquire_slowpath(lock) : 0;
}

void
rstm::release(rstm::tatas_lock_t& lock) {
    __sync_lock_release(&lock);
    // CFENCE;
    // *lock = 0;
}

int
rstm::acquire(rstm::ticket_lock_t& lock) {
    int ret = 0;
    for (uintptr_t my_ticket = __sync_fetch_and_add(&lock.next_ticket, 1);
         lock.now_serving != my_ticket; ++ret)
        ;
    return ret;
}

void
rstm::release(rstm::ticket_lock_t& lock) {
    lock.now_serving += 1;
}

int
rstm::acquire(rstm::mcs_qnode_t** lock, rstm::mcs_qnode_t* mine) {
    // init my qnode, then swap it into the root pointer
    mine->next = 0;
    rstm::mcs_qnode_t* pred = __sync_lock_test_and_set(lock, mine);

    // now set my flag, point pred to me, and wait for my flag to be unset
    int ret = 0;
    if (pred != 0) {
        mine->flag = true;
        pred->next = mine;
        while (mine->flag)
            ++ret; // spin
    }
    return ret;
}

void
rstm::release(rstm::mcs_qnode_t** lock, rstm::mcs_qnode_t* mine) {
    // if someone's already waiting on me, or someone arrives before I can
    // remove myself, then notify them, with the caveat that I need to wait for
    // their insert to finish
    if (mine->next != 0) {
        mine->next->flag = false;
        return;
    }

    if (__sync_bool_compare_and_swap(lock, mine, NULL))
        return;

    while (mine->next == 0)
        ; // wait for insertion to complete

    mine->next->flag = false;
}
