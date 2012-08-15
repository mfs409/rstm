/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef NOREC_HPP__
#define NOREC_HPP__

/**
 *  NOrec Implementation
 *
 *    This STM was published by Dalessandro et al. at PPoPP 2010.  The
 *    algorithm uses a single sequence lock, along with value-based validation,
 *    for concurrency control.  This variant offers semantics at least as
 *    strong as Asymmetric Lock Atomicity (ALA).
 */

#include "../cm.hpp"
#include "algs.hpp"

namespace stm
{
  // [mfs] Why this?
  const uintptr_t VALIDATION_FAILED = 1;

  template <class CM>
  TM_FASTCALL void NOrecGenericCommitRO(TX_LONE_PARAMETER);
  template <class CM>
  TM_FASTCALL void NOrecGenericCommitRW(TX_LONE_PARAMETER);
  template <class CM>
  TM_FASTCALL void* NOrecGenericReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  template <class CM>
  TM_FASTCALL void* NOrecGenericReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  template <class CM>
  TM_FASTCALL void NOrecGenericWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  template <class CM>
  TM_FASTCALL void NOrecGenericWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));

  template <class CM>
  NOINLINE
  uintptr_t NOrecGenericValidate(TxThread* tx)
  {
      while (true) {
          // read the lock until it is even
          uintptr_t s = timestamp.val;
          if ((s & 1) == 1)
              continue;

          // check the read set
          CFENCE;
          // don't branch in the loop---consider it backoff if we fail
          // validation early
          bool valid = true;
          foreach (ValueList, i, tx->vlist)
              valid &= STM_LOG_VALUE_IS_VALID(i, tx);

          if (!valid)
              return VALIDATION_FAILED;

          // restart if timestamp changed during read set iteration
          CFENCE;
          if (timestamp.val == s)
              return s;
      }
  }

  template <class CM>
  bool NOrecGenericIrrevoc(TxThread* tx)
  {
      while (!bcasptr(&timestamp.val, tx->start_time, tx->start_time + 1))
          if ((tx->start_time = NOrecGenericValidate<CM>(tx)) == VALIDATION_FAILED)
              return false;

      // redo writes
      tx->writes.writeback();

      // Release the sequence lock, then clean up
      CFENCE;
      timestamp.val = tx->start_time + 2;
      tx->vlist.reset();
      tx->writes.reset();
      return true;
  }

  template <class CM>
  void NOrecGenericOnSwitchTo() {
      // We just need to be sure that the timestamp is not odd, or else we
      // will block.  For safety, increment the timestamp to make it even, in
      // the event that it is odd.
      if (timestamp.val & 1)
          ++timestamp.val;
  }

  template <class CM>
  void NOrecGenericBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Originally, NOrec required us to wait until the timestamp is odd
      // before we start.  However, we can round down if odd, in which case
      // we don't need control flow here.

      // Sample the sequence lock, if it is even decrement by 1
      tx->start_time = timestamp.val & ~(1L);

      // notify the allocator
      tx->allocator.onTxBegin();

      // notify CM
      CM::onBegin(tx);
  }

  template <class CM>
  TM_FASTCALL
  void NOrecGenericCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Since all reads were consistent, and no writes were done, the read-only
      // NOrec transaction just resets itself and is done.
      CM::onCommit(tx);
      tx->vlist.reset();
      OnROCommit(tx);
  }

  template <class CM>
  TM_FASTCALL
  void NOrecGenericCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // From a valid state, the transaction increments the seqlock.  Then it does
      // writeback and increments the seqlock again

      // get the lock and validate (use RingSTM obstruction-free technique)
      while (!bcasptr(&timestamp.val, tx->start_time, tx->start_time + 1))
          if ((tx->start_time = NOrecGenericValidate<CM>(tx)) == VALIDATION_FAILED)
              tmabort();

      tx->writes.writeback();

      // Release the sequence lock, then clean up
      CFENCE;
      timestamp.val = tx->start_time + 2;

      // notify CM
      CM::onCommit(tx);

      tx->vlist.reset();
      tx->writes.reset();

      // This switches the thread back to RO mode.
      OnRWCommit(tx);
      ResetToRO(tx, NOrecGenericReadRO<CM>, NOrecGenericWriteRO<CM>, NOrecGenericCommitRO<CM>);
  }

  template <class CM>
  TM_FASTCALL
  void* NOrecGenericReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // A read is valid iff it occurs during a period where the seqlock does
      // not change and is even.  This code also polls for new changes that
      // might necessitate a validation.

      // read the location to a temp
      void* tmp = *addr;
      CFENCE;

      // if the timestamp has changed since the last read, we must validate and
      // restart this read
      while (tx->start_time != timestamp.val) {
          if ((tx->start_time = NOrecGenericValidate<CM>(tx)) == VALIDATION_FAILED)
              tmabort();
          tmp = *addr;
          CFENCE;
      }

      // log the address and value, uses the macro to deal with
      // STM_PROTECT_STACK
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  template <class CM>
  TM_FASTCALL
  void* NOrecGenericReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK(found, log, mask);

      // Use the code from the read-only read barrier. This is complicated by
      // the fact that, when we are byte logging, we may have successfully read
      // some bytes from the write log (if we read them all then we wouldn't
      // make it here). In this case, we need to log the mask for the rest of the
      // bytes that we "actually" need, which is computed as bytes in mask but
      // not in log.mask. This is only correct because we know that a failed
      // find also reset the log.mask to 0 (that's part of the find interface).
      void* val = NOrecGenericReadRO<CM>(TX_FIRST_ARG addr STM_MASK(mask & ~log.mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  template <class CM>
  TM_FASTCALL
  void NOrecGenericWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // buffer the write, and switch to a writing context
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, NOrecGenericReadRW<CM>, NOrecGenericWriteRW<CM>, NOrecGenericCommitRW<CM>);
  }

  template <class CM>
  TM_FASTCALL
  void NOrecGenericWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // just buffer the write
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  template <class CM>
  void NOrecGenericRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // notify CM
      CM::onAbort(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      tx->vlist.reset();
      tx->writes.reset();
      PostRollback(tx);
      ResetToRO(tx, NOrecGenericReadRO<CM>, NOrecGenericWriteRO<CM>, NOrecGenericCommitRO<CM>);
  }

}

#endif // NOREC_HPP__
