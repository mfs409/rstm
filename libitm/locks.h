/// -*- C++ -*-
/// Copyright (C) 2012
/// University of Rochester Department of Computer Science
///   and
/// Lehigh University Department of Computer Science and Engineering
///
/// License: Modified BSD
///          Please see the file LICENSE.RSTM for licensing information
///
#ifndef RSTM_UTILS_LOCKS_H
#define RSTM_UTILS_LOCKS_H

#include <stdint.h>

namespace rstm {

  extern "C" typedef volatile uintptr_t tatas_lock_t;
  int acquire(tatas_lock_t&);
  void release(tatas_lock_t&);

  extern "C" typedef struct ticket_lock {
      volatile uintptr_t next_ticket;
      volatile uintptr_t now_serving;
  } ticket_lock_t;

  int acquire(ticket_lock_t&);
  void release(ticket_lock_t&);

  extern "C" typedef struct mcs_qnode {
      volatile bool flag;
      volatile struct mcs_qnode* volatile next;
  } mcs_qnode_t;

  int acquire(mcs_qnode_t** lock, mcs_qnode_t* mine);
  void release(mcs_qnode_t** lock, mcs_qnode_t* mine);
}

#endif // RSTM_UTILS_LOCKS_H
