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
#include "txthread.hpp"
#include "profiling.hpp"

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

  // This is used as a default in txthread.cpp... just forwards to CGL::begin.
  void begin_CGL(TX_LONE_PARAMETER);

  typedef TM_FASTCALL void* (*ReadBarrier)(TX_FIRST_PARAMETER STM_READ_SIG(,));
  typedef TM_FASTCALL void (*WriteBarrier)(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  typedef TM_FASTCALL void (*CommitBarrier)(TX_LONE_PARAMETER );

#ifndef STM_ONESHOT_MODE
  inline void SetLocalPointers(ReadBarrier r, WriteBarrier w, CommitBarrier c)
  {
      tmread = r;
      tmwrite = w;
      tmcommit = c;
  }
#endif

#ifndef STM_ONESHOT_MODE
  inline void OnReadWriteCommit(TxThread* tx, ReadBarrier read_ro,
                                WriteBarrier write_ro, CommitBarrier commit_ro)
#else
  inline void OnReadWriteCommit(TxThread* tx, ReadBarrier, WriteBarrier,
                                CommitBarrier)
#endif
  {
      tx->allocator.onTxCommit();
      tx->abort_hist.onCommit(tx->consec_aborts);
      tx->consec_aborts = 0;
      tx->consec_ro = 0;
      ++tx->num_commits;
#ifndef STM_ONESHOT_MODE
      SetLocalPointers(read_ro, write_ro, commit_ro);
#else
      tx->mode = MODE_RO;
#endif
      Trigger::onCommitSTM(tx);
  }

  inline void OnReadWriteCommit(TxThread* tx)
  {
      tx->allocator.onTxCommit();
      tx->abort_hist.onCommit(tx->consec_aborts);
      tx->consec_aborts = 0;
      tx->consec_ro = 0;
      ++tx->num_commits;
      Trigger::onCommitSTM(tx);
  }

  inline void OnReadOnlyCommit(TxThread* tx)
  {
      tx->allocator.onTxCommit();
      tx->abort_hist.onCommit(tx->consec_aborts);
      tx->consec_aborts = 0;
      ++tx->consec_ro;
      ++tx->num_ro;
      Trigger::onCommitSTM(tx);
  }

  inline void OnCGLCommit(TxThread* tx)
  {
      tx->allocator.onTxCommitImmediate();
      tx->consec_ro = 0;
      ++tx->num_commits;
      Trigger::onCommitLock(tx);
  }

  inline void OnReadOnlyCGLCommit(TxThread* tx)
  {
      tx->allocator.onTxCommit();
      ++tx->consec_ro;
      ++tx->num_ro;
      Trigger::onCommitLock(tx);
  }

#ifndef STM_ONESHOT_MODE
  inline void OnFirstWrite(TxThread*, ReadBarrier read_rw, WriteBarrier write_rw,
                           CommitBarrier commit_rw)
  {
      SetLocalPointers(read_rw, write_rw, commit_rw);
  }
#else
  inline void OnFirstWrite(TxThread* tx, ReadBarrier, WriteBarrier, CommitBarrier)
  {
      tx->mode = MODE_WRITE;
  }
#endif

  inline void PreRollback(TxThread* tx)
  {
      ++tx->num_aborts;
      ++tx->consec_aborts;
  }

#ifndef STM_ONESHOT_MODE
  inline void PostRollback(TxThread* tx, ReadBarrier read_ro,
                           WriteBarrier write_ro, CommitBarrier commit_ro)
#else
  inline void PostRollback(TxThread* tx, ReadBarrier, WriteBarrier,
                           CommitBarrier)
#endif
  {
      tx->allocator.onTxAbort();
      tx->nesting_depth = 0;
#ifndef STM_ONESHOT_MODE
      SetLocalPointers(read_ro, write_ro, commit_ro);
#else
      tx->mode = MODE_RO;
#endif
      Trigger::onAbort(tx);
      tx->in_tx = false;
  }

  inline void PostRollback(TxThread* tx)
  {
      tx->allocator.onTxAbort();
      tx->nesting_depth = 0;
      Trigger::onAbort(tx);
      tx->in_tx = false;
  }

  /**
   *  Custom PostRollback code for ProfileTM.  If a transaction other than
   *  the last in the profile set aborts, we roll it back using this
   *  function, which does everything the prior PostRollback did except for
   *  calling the "Trigger::onAbort()" method.
   */
  #ifndef STM_ONESHOT_MODE
  inline void PostRollbackNoTrigger(TxThread* tx, ReadBarrier r,
                                    WriteBarrier w, CommitBarrier c)
#else
  inline void PostRollbackNoTrigger(TxThread* tx, ReadBarrier,
                                    WriteBarrier, CommitBarrier)
#endif
  {
      tx->allocator.onTxAbort();
      tx->nesting_depth = 0;
#ifndef STM_ONESHOT_MODE
      SetLocalPointers(r, w, c);
#else
      tx->mode = MODE_RO;
#endif
      tx->in_tx = false;
  }

  /**
  *  Custom PostRollback code for ProfileTM.  If the last transaction in the
  *  profile set aborts, it will call profile_oncomplete before calling this.
  *  That means that it will adapt /out of/ ProfileTM, which in turn means
  *  that we cannot reset the pointers on abort.
  */
  inline void PostRollbackNoTrigger(TxThread* tx)
  {
      tx->allocator.onTxAbort();
      tx->nesting_depth = 0;
      tx->in_tx = false;
  }

#ifndef STM_ONESHOT_MODE
  inline void GoTurbo(TxThread*, ReadBarrier r, WriteBarrier w, CommitBarrier c)
  {
      SetLocalPointers(r, w, c);
  }

  inline bool CheckTurboMode(TxThread*, ReadBarrier r)
  {
      return (tmread == r);
  }

#else
  inline void GoTurbo(TxThread* tx, ReadBarrier, WriteBarrier, CommitBarrier)
  {
      tx->mode = MODE_TURBO;
  }

  inline bool CheckTurboMode(TxThread* tx, ReadBarrier)
  {
      return tx->mode == MODE_TURBO;
  }
#endif

} // namespace stm

#ifndef STM_ONESHOT_MODE
#define DECLARE_AS_ONESHOT_TURBO(CLASS)
#define DECLARE_AS_ONESHOT_NORMAL(CLASS)
#define DECLARE_AS_ONESHOT_SIMPLE(CLASS)
#else
#define DECLARE_AS_ONESHOT_TURBO(CLASS)                                 \
namespace stm                                                           \
{                                                                       \
    TM_FASTCALL void* tmread(TX_FIRST_PARAMETER STM_READ_SIG(addr,))    \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (tx->mode == stm::MODE_TURBO)                                \
            return CLASS::read_turbo(TX_FIRST_ARG addr STM_MASK(mask)); \
        else if (tx->mode == stm::MODE_WRITE)                           \
            return CLASS::read_rw(TX_FIRST_ARG addr STM_MASK(mask));    \
        else                                                            \
            return CLASS::read_ro(TX_FIRST_ARG addr STM_MASK(mask));    \
    }                                                                   \
    TM_FASTCALL void tmwrite(TX_FIRST_PARAMETER                         \
                             STM_WRITE_SIG(addr,value,mask))            \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (tx->mode == stm::MODE_TURBO)                                \
            CLASS::write_turbo(TX_FIRST_ARG addr,                       \
                               value STM_MASK(mask));                   \
        else if (tx->mode == stm::MODE_WRITE)                           \
            CLASS::write_rw(TX_FIRST_ARG addr, value STM_MASK(mask));   \
        else                                                            \
            CLASS::write_ro(TX_FIRST_ARG addr, value STM_MASK(mask));   \
    }                                                                   \
    TM_FASTCALL void tmcommit(TX_LONE_PARAMETER)                        \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (tx->mode == stm::MODE_TURBO)                                \
            CLASS::commit_turbo(TX_LONE_ARG);                           \
        else if (tx->mode == stm::MODE_WRITE)                           \
            CLASS::commit_rw(TX_LONE_ARG);                              \
        else                                                            \
            CLASS::commit_ro(TX_LONE_ARG);                              \
    }                                                                   \
}

#define DECLARE_AS_ONESHOT_NORMAL(CLASS)                                \
namespace stm                                                           \
{                                                                       \
    TM_FASTCALL void* tmread(TX_FIRST_PARAMETER STM_READ_SIG(addr,))    \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (tx->mode == stm::MODE_WRITE)                                \
            return CLASS::read_rw(TX_FIRST_ARG addr STM_MASK(mask));    \
        else                                                            \
            return CLASS::read_ro(TX_FIRST_ARG addr STM_MASK(mask));    \
    }                                                                   \
    TM_FASTCALL void tmwrite(TX_FIRST_PARAMETER                         \
                             STM_WRITE_SIG(addr,value,mask))            \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (tx->mode == stm::MODE_WRITE)                                \
            CLASS::write_rw(TX_FIRST_ARG addr, value STM_MASK(mask));   \
        else                                                            \
            CLASS::write_ro(TX_FIRST_ARG addr, value STM_MASK(mask));   \
    }                                                                   \
    TM_FASTCALL void tmcommit(TX_LONE_PARAMETER)                        \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (tx->mode == stm::MODE_WRITE)                                \
            CLASS::commit_rw(TX_LONE_ARG);                              \
        else                                                            \
            CLASS::commit_ro(TX_LONE_ARG);                              \
    }                                                                   \
}

#define DECLARE_AS_ONESHOT_SIMPLE(CLASS)                                \
namespace stm                                                           \
{                                                                       \
    TM_FASTCALL void* tmread(TX_FIRST_PARAMETER STM_READ_SIG(addr,))    \
    {                                                                   \
        return CLASS::read(TX_FIRST_ARG addr STM_MASK(mask));           \
    }                                                                   \
    TM_FASTCALL void tmwrite(TX_FIRST_PARAMETER                         \
                             STM_WRITE_SIG(addr,value,mask))            \
    {                                                                   \
        CLASS::write(TX_FIRST_ARG addr, value STM_MASK(mask));          \
    }                                                                   \
    TM_FASTCALL void tmcommit(TX_LONE_PARAMETER)                        \
    {                                                                   \
        CLASS::commit(TX_LONE_ARG);                                     \
    }                                                                   \
}

#endif

#endif // INST_HPP__
