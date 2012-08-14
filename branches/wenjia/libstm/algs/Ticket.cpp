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
 *  Ticket Implementation
 *
 *    This STM uses a single ticket lock for all concurrency control.  There is
 *    no parallelism, but it is very fair.
 */

#include "algs.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  TM_FASTCALL void* TicketRead(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void TicketWrite(TX_FIRST_PARAMETER STM_WRITE_SIG(,,  ));
  TM_FASTCALL void TicketCommit(TX_LONE_PARAMETER);

  /**
   *  Ticket begin:
   */
  void TicketBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // get the ticket lock
      tx->begin_wait = ticket_acquire(&ticketlock);
      tx->allocator.onTxBegin();
  }

  /**
   *  Ticket commit:
   */
  void TicketCommit(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // release the lock, finalize mm ops, and log the commit
      ticket_release(&ticketlock);
      OnCGLCommit(tx);
  }

  /**
   *  Ticket read
   */
  void* TicketRead(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      return *addr;
  }

  /**
   *  Ticket write
   */
  void TicketWrite(TX_FIRST_PARAMETER_ANON STM_WRITE_SIG(addr,val,mask))
  {
      STM_DO_MASKED_WRITE(addr, val, mask);
  }

  /**
   *  Ticket unwinder:
   *
   *    In Ticket, aborts are never valid
   */
  void
  TicketRollback(STM_ROLLBACK_SIG(,,))
  {
      UNRECOVERABLE("ATTEMPTING TO ABORT AN IRREVOCABLE TICKET TRANSACTION");
  }

  /**
   *  Ticket in-flight irrevocability:
   *
   *    Since we're already irrevocable, this code should never get called.
   *    Instead, the become_irrevoc(TX_LONE_PARAMETER) call should just return true.
   */
  bool
  TicketIrrevoc(TxThread*)
  {
      UNRECOVERABLE("IRREVOC_TICKET SHOULD NEVER BE CALLED");
      return false;
  }

  /**
   *  Switch to Ticket:
   *
   *    For now, no other algs use the ticketlock variable, so no work is needed
   *    in this function.
   */
  void
  TicketOnSwitchTo() {
  }

  /**
   *  Ticket initialization
   */
  template<>
  void initTM<Ticket>()
  {
      // set the name
      stms[Ticket].name      = "Ticket";

      // set the pointers
      stms[Ticket].begin     = TicketBegin;
      stms[Ticket].commit    = TicketCommit;
      stms[Ticket].read      = TicketRead;
      stms[Ticket].write     = TicketWrite;
      stms[Ticket].rollback  = TicketRollback;
      stms[Ticket].irrevoc   = TicketIrrevoc;
      stms[Ticket].switcher  = TicketOnSwitchTo;
      stms[Ticket].privatization_safe = true;
  }
}

#ifdef STM_ONESHOT_ALG_Ticket
DECLARE_AS_ONESHOT_SIMPLE(Ticket)
#endif
