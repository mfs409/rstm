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
 *  TML Implementation
 *
 *    This STM was published by Dalessandro et al. at EuroPar 2010.  The
 *    algorithm allows multiple readers or a single irrevocable writer.  The
 *    semantics are at least as strong as ALA.
 *
 *    NB: now that we dropped the inlined-tml instrumentation hack, we should
 *        probably add ro/rw functions
 */

#include "algs.hpp"
#include "tml_inline.hpp"
#include "../Diagnostics.hpp"

namespace stm
{
  TM_FASTCALL void* TMLRead(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void TMLWrite(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void TMLCommit(TX_LONE_PARAMETER);

  /**
   *  TML begin:
   */
  void TMLBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      int counter = 0;
      // Sample the sequence lock until it is even (unheld)
      while ((tx->start_time = timestamp.val) & 1) {
          spin64();
          counter += 64;
      }

      // notify the allocator
      tx->begin_wait = counter;
      tx->allocator.onTxBegin();
  }

  /**
   *  TML commit:
   */
  void
  TMLCommit(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // writing context: release lock, free memory, remember commit
      if (tx->tmlHasLock) {
          ++timestamp.val;
          tx->tmlHasLock = false;
          OnRWCommit(tx);
      }
      // reading context: just remember the commit
      else {
          OnROCommit(tx);
      }
      Trigger::onCommitLock(tx);
  }

  /**
   *  TML read:
   *
   *    If we have the lock, we're irrevocable so just do a read.  Otherwise,
   *    after doing the read, make sure we are still valid.
   */
  void* TMLRead(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      TX_GET_TX_INTERNAL;
      void* val = *addr;
      if (tx->tmlHasLock)
          return val;
      // NB:  afterread_tml includes a CFENCE
      afterread_TML(tx);
      return val;
  }

  /**
   *  TML write:
   *
   *    If we have the lock, do an in-place write and return.  Otherwise, we
   *    need to become irrevocable first, then do the write.
   */
  void TMLWrite(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      if (tx->tmlHasLock) {
          STM_DO_MASKED_WRITE(addr, val, mask);
          return;
      }
      // NB:  beforewrite_tml includes a fence via CAS
      beforewrite_TML(tx);
      STM_DO_MASKED_WRITE(addr, val, mask);
  }

  /**
   *  TML unwinder
   *
   *    NB: This should not be called from a writing context!  That means
   *        calling restart(TX_LONE_PARAMETER) under TML with writes is not allowed, but we
   *        don't currently enforce.
   *
   *    NB: don't need to worry about exception object since anyone rolling
   *        back must be read-only, and thus the logs have no writes to
   *        exception objects pending.
   */
  void
  TMLRollback(STM_ROLLBACK_SIG(tx,,))
  {
      PreRollback(tx);
      PostRollback(tx);
  }

  /**
   *  TML in-flight irrevocability:
   *
   *    We have a custom path for going irrevocable in TML, so this code should
   *    never be called.
   */
  bool
  TMLIrrevoc(TxThread*)
  {
      UNRECOVERABLE("IRREVOC_TML SHOULD NEVER BE CALLED");
      return false;
  }

  /**
   *  Switch to TML:
   *
   *    We just need to be sure that the timestamp is not odd, or else we will
   *    block.  For safety, increment the timestamp to make it even, in the
   *    event that it is odd.
   */
  void
  TMLOnSwitchTo()
  {
      if (timestamp.val & 1)
          ++timestamp.val;
  }
}

REGISTER_REGULAR_ALG(TML, "TML", true)

#ifdef STM_ONESHOT_ALG_TML
DECLARE_AS_ONESHOT(TML)
#endif
