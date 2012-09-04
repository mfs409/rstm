/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.ISM for licensing information
 */

/**
 *  This file presents a simple library API for using RSTM without compiler
 *  support.  The API consists of the following:
 *
 *  TM_ALLOC            : Allocate memory inside a transaction
 *  TM_FREE             : Deallocate memory inside a transaction
 *  TM_SYS_INIT         : Initialize the STM library
 *  TM_SYS_SHUTDOWN     : Shut down the STM library
 *  TM_THREAD_INIT      : Initialize a thread before using TM
 *  TM_THREAD_SHUTDOWN  : Shut down a thread
 *  TM_SET_POLICY(P)    : Change the STM algorithm on the fly
 *  TM_BECOME_IRREVOC() : Become irrevocable or abort
 *  TM_READ(var)        : Read from shared memory from a txn
 *  TM_WRITE(var, val)  : Write to shared memory from a txn
 *  TM_BEGIN(type)      : Start a transaction... use 'atomic' as type
 *  TM_END              : End a transaction
 *
 *  Custom Features:
 *
 *  stm::restart()                : Self-abort and immediately retry a txn
 *  TM_BEGIN_FAST_INITIALIZATION  : For fast initialization
 *  TM_END_FAST_INITIALIZATION    : For fast initialization
 *  TM_GET_ALGNAME()              : Get the current algorithm name
 *
 *  Compiler Compatibility::Annotations (unused in library):
 *
 *  TM_WAIVER        : mark a block that does not get TM instrumentation
 *  TM_CALLABLE      : mark a function as being callable by TM
 */

#ifndef RSTM_HPP__
#define RSTM_HPP__

#include <setjmp.h>
#include "abstract_compiler.hpp"
#include "macros.hpp"
#include "ThreadLocal.hpp"
#include "tlsapi.hpp"

#ifdef STM_CHECKPOINT_ASM
#include "../libstm/libitm.h"
// [mfs] TODO: need to move the include out of libstm if we use it here
extern uint32_t _ITM_beginTransaction(uint32_t)
    ITM_REGPARM __attribute__((returns_twice));
// [mfs] TODO: some adaptivity stuff is not correct inside of
// _ITM_beginTransaction... custom ASM work is still needed
#endif

namespace stm
{
#ifndef STM_CHECKPOINT_ASM

  /**
   *  To ensure a proper signature on begin, we need to have something called
   *  "stm::scope_t"
   */
  typedef void scope_t;

  /**
   *  Code to start a transaction.  We assume the caller already performed a
   *  setjmp, and is passing a valid setjmp buffer to this function.
   */
  void begin(TX_FIRST_PARAMETER scope_t* s, uint32_t abort_flags);
#endif

  /**
   *  Code to commit a transaction.
   */
  void commit(TX_LONE_PARAMETER);

  /**
   *  The STM system provides a message that exits the program (preferable to
   *  'assert(false)').  We use this in the API too, so it needs to be visible
   *  here
   */
  void NORETURN UNRECOVERABLE(const char*);

  /**
   *  This portion of the API addresses allocation.  We provide tx-safe malloc
   *  and free calls, which also work from nontransactional contexts.
   */

  /**
   *  get a chunk of memory that will be automatically reclaimed if the caller
   *  is a transaction that ultimately aborts
   */
  void* tx_alloc(size_t size);

  /**
   *  Free some memory.  If the caller is a transaction that ultimately aborts,
   *  the free will not happen.  If the caller is a transaction that commits,
   *  the free will happen at commit time.
   */
  void tx_free(void* p);

  /**
   *  Here we declare the rest of the api to the STM library
   */

  /**
   *  Initialize the library (call before doing any per-thread initialization)
   *
   *  We rely on the default setjmp/longjmp abort handling when using the
   *  library API.
   */
  void sys_init();

  /**
   *  Shut down the library.  This just dumps some statistics.
   */
  void sys_shutdown();

  /***  Set up a thread's transactional context */
  void thread_init();

  /***  Shut down a thread's transactional context */
  void thread_shutdown();

  /**
   *  Set the current STM algorithm/policy.  This should be called at the
   *  beginning of each program phase
   */
  void set_policy(const char*);

  /***  Report the algorithm name that was used to initialize libstm */
  const char* get_algname();

  /**
   *  Become irrevocable.  Call this from within a transaction.
   */
  void become_irrevoc();

  /**
   *  Abort the current transaction and restart immediately.
   */
  void restart();

  /**
   * Declare the next transaction of this thread to be read-only (hack for
   * pessimistic STM)
   */
  void declare_read_only();

#if defined(STM_INST_FINEGRAINADAPT)

#ifdef STM_OS_MACOS
   extern stm::tls::ThreadLocal<__attribute__((regparm(3))) void*(*)(stm::TxThread* tx, void** ), sizeof(void*)> tmread;
   extern stm::tls::ThreadLocal<__attribute__((regparm(3))) void(*)(stm::TxThread* tx, void** , void*), sizeof(void*)> tmwrite;
#else
  /*** Per-thread commit, read, and write pointers */
  extern THREAD_LOCAL_DECL_TYPE(TM_FASTCALL void*(*tmread)(TX_FIRST_PARAMETER STM_READ_SIG(,)));
  extern THREAD_LOCAL_DECL_TYPE(TM_FASTCALL void(*tmwrite)(TX_FIRST_PARAMETER STM_WRITE_SIG(,,)));
#endif

#elif defined(STM_INST_COARSEGRAINADAPT)
  /*** Per-thread commit, read, and write pointers */
  extern TM_FASTCALL void*(*tmread)(TX_FIRST_PARAMETER STM_READ_SIG(,));
  extern TM_FASTCALL void(*tmwrite)(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));

#else
  TM_FASTCALL void* tmread(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void  tmwrite(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));

#endif
}

/*** pull in the per-memory-access instrumentation framework */
#include "library_inst.hpp"

/**
 *  Now we can make simple macros for reading and writing shared memory, by
 *  using templates to dispatch to the right code:
 */
namespace stm
{
  template <typename T>
  inline T stm_read(TX_FIRST_PARAMETER T* addr)
  {
      return DISPATCH<T, sizeof(T)>::read(TX_FIRST_ARG addr);
  }

  template <typename T>
  inline void stm_write(TX_FIRST_PARAMETER T* addr, T val)
  {
      DISPATCH<T, sizeof(T)>::write(TX_FIRST_ARG addr, val);
  }
} // namespace stm

/**
 * Code should only use these calls, not the template stuff declared above
 */
#define TM_READ(var)       stm::stm_read(TX_FIRST_ARG &var)
#define TM_WRITE(var, val) stm::stm_write(TX_FIRST_ARG &var, val)

#ifdef STM_CHECKPOINT_ASM

/**
 *  This is the way to start a transaction
 */
#define TM_BEGIN(TYPE)                                      \
    {                                                       \
    _ITM_beginTransaction();                                \
    {                                                       \


/**
 *  [wer210] This is the way to start a Read-Only transaction.
 *  Only used for Pessimistic TM for now!
 *  set tx->read_only true.
 */
#define TM_BEGIN_READONLY(TYPE)                             \
    {                                                       \
    stm::declare_read_only();                               \
    _ITM_beginTransaction();                                \
    {

#else
/**
 *  This is the way to start a transaction
 */
#define TM_BEGIN(TYPE)                                      \
    {                                                       \
    TX_GET_TX;                                              \
    jmp_buf _jmpbuf;                                        \
    uint32_t abort_flags = setjmp(_jmpbuf);                 \
    stm::begin(TX_FIRST_ARG &_jmpbuf, abort_flags);         \
    CFENCE;                                                 \
    {

/**
 *  [wer210] This is the way to start a Read-Only transaction.
 *  Only used for Pessimistic TM for now!
 *  set tx->read_only true.
 */
#define TM_BEGIN_READONLY(TYPE)                             \
    {                                                       \
    TX_GET_TX;                                              \
    jmp_buf _jmpbuf;                                        \
    uint32_t abort_flags = setjmp(_jmpbuf);                 \
    stm::declare_read_only();                               \
    stm::begin(TX_FIRST_ARG &_jmpbuf, abort_flags);         \
    CFENCE;                                                 \
    {
#endif

/**
 *  This is the way to commit a transaction.  Note that these macros weakly
 *  enforce lexical scoping
 */
#define TM_END                                  \
    }                                           \
    stm::commit(TX_LONE_ARG);                   \
    }

#define TM_CALLABLE
#define TM_WAIVER
#define TM_SYS_INIT()        stm::sys_init()
#define TM_THREAD_INIT       stm::thread_init
#define TM_THREAD_SHUTDOWN() stm::thread_shutdown()
#define TM_SYS_SHUTDOWN      stm::sys_shutdown
#define TM_ALLOC             stm::tx_alloc
#define TM_FREE              stm::tx_free
#define TM_SET_POLICY(P)     stm::set_policy(P)
#define TM_BECOME_IRREVOC()  stm::become_irrevoc()
#define TM_GET_ALGNAME()     stm::get_algname()

/**
 * This is gross.  ITM, like any good compiler, will make nontransactional
 * versions of code so that we can cleanly do initialization from outside of
 * a transaction.  The library **can** do this, but only via some cumbersome
 * template games that we really don't want to keep playing (see the previous
 * release for examples).
 *
 * Since we don't want to have transactional configuration (it is slow, and
 * it messes up some accounting of commits and transaction sizes), we use the
 * following trick: if we aren't using a compiler for instrumentation, then
 * BEGIN_FAST_INITIALIZATION will copy the current STM configuration (envar
 * STM_CONFIG) to a local, then switch the mode to CGL, then call the
 * instrumented functions using CGL instrumentation (e.g., the lightest
 * possible, and correct without a 'commit').  Likewise, if we aren't using a
 * compiler for instrumentation, then END_FAST_INITIALIZATION will restore
 * the original configuration, so that the app will use the STM as expected.
 */
#ifdef STM_API_ITM
#  define TM_BEGIN_FAST_INITIALIZATION()
#  define TM_END_FAST_INITIALIZATION()
#elif defined STM_INST_ONESHOT
#  define TM_BEGIN_FAST_INITIALIZATION() TM_BEGIN(atomic)
#  define TM_END_FAST_INITIALIZATION()   TM_END
#else
#  define TM_BEGIN_FAST_INITIALIZATION()                \
    const char* __config_string__ = TM_GET_ALGNAME();   \
    TM_SET_POLICY("CGL");\
    TX_GET_TX
#  define TM_END_FAST_INITIALIZATION()          \
    TM_SET_POLICY(__config_string__)
#endif

#endif // RSTM_HPP__
