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
 *  NOrecPrio Implementation
 *
 *    This is like NOrec, except that too many consecutive aborts result in
 *    this thread gaining priority.  When a thread has priority, lower-priority
 *    threads cannot commit if they are writers
 */

#include "algs.hpp"

namespace stm
{
  // [mfs] Get rid of this...
  static const uintptr_t VALIDATION_FAILED = 1;

  TM_FASTCALL void* NOrecPrioReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void* NOrecPrioReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void NOrecPrioWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void NOrecPrioWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  TM_FASTCALL void NOrecPrioCommitRO(TX_LONE_PARAMETER);
  TM_FASTCALL void NOrecPrioCommitRW(TX_LONE_PARAMETER);
  NOINLINE uintptr_t NOrecPrioValidate(TxThread*);

  /**
   *  NOrecPrio begin:
   *
   *    We're using the 'classic' NOrec begin technique here.  Also, we check if
   *    we need priority here, rather than retaining it across an abort.
   */
  void NOrecPrioBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // Sample the sequence lock until it is even (unheld)
      while ((tx->start_time = timestamp.val) & 1)
          spin64();

      // notify the allocator
      tx->allocator.onTxBegin();

      // handle priority
      long prio_bump = tx->consec_aborts / KARMA_FACTOR;
      if (prio_bump) {
          faiptr(&prioTxCount.val);
          tx->prio = prio_bump;
      }
  }

  /**
   *  NOrecPrio commit (read-only):
   *
   *    Standard NOrec RO commit, except that if we have priority, we must
   *    release it.
   */
  void
  NOrecPrioCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // read-only fastpath
      tx->vlist.reset();
      // priority
      if (tx->prio) {
          faaptr(&prioTxCount.val, -1);
          tx->prio = 0;
      }
      OnROCommit(tx);
  }

  /**
   *  NOrecPrio commit (writing context):
   *
   *    This priority technique is imprecise.  Someone could gain priority while
   *    this thread is trying to acquire the CAS.  That's OK, because we just aim
   *    to be "fair", without any guarantees.
   */
  void
  NOrecPrioCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // wait for all higher-priority transactions to complete
      //
      // NB: we assume there are priority transactions, because we wouldn't be
      //     using the STM otherwise.
      while (true) {
          bool good = true;
          for (uintptr_t i = 0; i < threadcount.val; ++i)
              good = good && (threads[i]->prio <= tx->prio);
          if (good)
              break;
      }

      // get the lock and validate (use RingSTM obstruction-free technique)
      while (!bcasptr(&timestamp.val, tx->start_time, tx->start_time + 1))
          if ((tx->start_time = NOrecPrioValidate(tx)) == VALIDATION_FAILED)
              tmabort();

      // redo writes
      tx->writes.writeback();

      // release the sequence lock, then clean up
      CFENCE;
      timestamp.val = tx->start_time + 2;
      tx->vlist.reset();
      tx->writes.reset();
      // priority
      if (tx->prio) {
          faaptr(&prioTxCount.val, -1);
          tx->prio = 0;
      }
      OnRWCommit(tx);
      ResetToRO(tx, NOrecPrioReadRO, NOrecPrioWriteRO, NOrecPrioCommitRO);
  }

  /**
   *  NOrecPrio read (read-only transaction)
   *
   *    This is a standard NOrec read
   */
  void*
  NOrecPrioReadRO(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // read the location to a temp
      void* tmp = *addr;
      CFENCE;

      while (tx->start_time != timestamp.val) {
          if ((tx->start_time = NOrecPrioValidate(tx)) == VALIDATION_FAILED)
              tmabort();
          tmp = *addr;
          CFENCE;
      }

      // log the address and value, uses the macro to deal with
      // STM_PROTECT_STACK
      STM_LOG_VALUE(tx, addr, tmp, mask);
      return tmp;
  }

  /**
   *  NOrecPrio read (writing transaction)
   *
   *    Standard NOrec read from writing context
   */
  void*
  NOrecPrioReadRW(TX_FIRST_PARAMETER STM_READ_SIG(addr,mask))
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
      void* val = NOrecPrioReadRO(TX_FIRST_ARG addr STM_MASK(mask & ~log.mask));
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  NOrecPrio write (read-only context)
   *
   *    log the write and switch to a writing context
   */
  void
  NOrecPrioWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // do a buffered write
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, NOrecPrioReadRW, NOrecPrioWriteRW, NOrecPrioCommitRW);
  }

  /**
   *  NOrecPrio write (writing context)
   *
   *    log the write
   */
  void
  NOrecPrioWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // do a buffered write
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  NOrecPrio unwinder:
   *
   *    If we abort, be sure to release priority
   */
  void
  NOrecPrioRollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      tx->vlist.reset();
      tx->writes.reset();
      // if I had priority, release it
      if (tx->prio) {
          faaptr(&prioTxCount.val, -1);
          tx->prio = 0;
      }
      PostRollback(tx);
      ResetToRO(tx, NOrecPrioReadRO, NOrecPrioWriteRO, NOrecPrioCommitRO);
  }

  /**
   *  NOrecPrio in-flight irrevocability: Getting priority right is very
   *  hard, so we're just going to use abort-and-restart
   */
  bool NOrecPrioIrrevoc(TxThread*)
  {
      return false;
  }

  /**
   *  NOrecPrio validation
   *
   *    Make sure that during some time period where the seqlock is constant
   *    and odd, all values in the read log are still present in memory.
   */
  uintptr_t
  NOrecPrioValidate(TxThread* tx)
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

  /**
   *  Switch to NOrecPrio:
   *
   *    Must be sure the timestamp is not odd.
   */
  void
  NOrecPrioOnSwitchTo()
  {
      if (timestamp.val & 1)
          ++timestamp.val;
  }
}

DECLARE_SIMPLE_METHODS_FROM_NORMAL(NOrecPrio)
REGISTER_FGADAPT_ALG(NOrecPrio, "NOrecPrio", true)

#ifdef STM_ONESHOT_ALG_NOrecPrio
DECLARE_AS_ONESHOT_NORMAL(NOrecPrio)
#endif
