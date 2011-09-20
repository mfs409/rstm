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
 *  Strictly speaking, any TM-supporting C++ compiler will not need this
 *  file.  However, if we want a program to be compilable with our library
 *  API, but also compilable with a TM C++ compiler, then we need to map the
 *  library calls to the calls the TM compiler expects.
 *
 *  NB: Right now this only works with the Intel compiler
 *
 *  NB: The above claim about "any compiler" is not entirely true for
 *      Oracle's compiler.  The specific issue is that the Oracle compiler
 *      does not use anything like setjmp/longjmp for validating and
 *      unwinding transactions.  This means that __transaction [[TYPE]]
 *      statements don't do any stack checkpointing.  Thus the "OTM"
 *      interface will actually rely on the use of a TM_BEGIN macro that
 *      first performs a setjmp and stores the address of the jump buffer in
 *      the transaction descriptor.
 */

#ifndef STM_API_CXXTM_HPP
#define STM_API_CXXTM_HPP

// this include is needed to make 'nop()' visible
#include <common/platform.hpp>

namespace stm
{
  /**
   *  Set the current STM algorithm/policy.  This should be called at the
   *  beginning of each program phase
   */
  void set_policy(const char*);

  /***  Report the algorithm name that was used to initialize libstm */
  const char* get_algname();
}

/**
 *  The translation of API macros to SHIM/Library functions is dependent on
 *  what compiler is being used.  The following applies to ICC, in both the
 *  case where we are doing an ITM2STM shim build, and the case where we are
 *  using Intel's TM library directly.
 */
#if defined(ITM) || defined(ITM2STM)

  // The prototype icc stm compiler version 4.0 doesn't understand transactional
  // malloc and free without some help. The ifdef guard could be more intelligent.
  #if defined(__ICC)
  extern "C" {
    [[transaction_safe]] void* malloc(size_t) __THROW;
    [[transaction_safe]] void free(void*) __THROW;
  }
  #endif

  // marker for a function that can be called from a transaction
  #define TM_CALLABLE         [[transaction_safe]]

  // markers for the beginning and end of a transaction
  #define TM_BEGIN(TYPE)      __transaction [[TYPE]] {
  #define TM_END              }

  // marker for a nontransactional region within a transaction
  #define TM_WAIVER           __transaction [[waiver]]

  // all descriptor-management stuff is meaningless when there is a compiler
  // transforming the code
  #define TM_GET_THREAD()
  #define TM_ARG
  #define TM_ARG_ALONE
  #define TM_PARAM
  #define TM_PARAM_ALONE

  // reads and writes just transform to naked reads and writes, since the
  // compiler will transform them after the macro preprocessor step
  #define TM_READ(x) (x)
  #define TM_WRITE(x, y) (x) = (y)

  // initialization and shutdown routines
  #define  TM_SYS_INIT                   _ITM_initializeProcess
  #define  TM_THREAD_INIT                _ITM_initializeThread
  #define  TM_THREAD_SHUTDOWN            _ITM_finalizeThread
  #define  TM_SYS_SHUTDOWN               _ITM_finalizeProcess

  // memory management
  #define  TM_ALLOC                      malloc
  #define  TM_FREE                       free

  /**
   * custom RSTM stuff for managing the current running algorithm.  These
   * methods only have meaning when using RSTM.  If we're using ITM, then they
   * are not interesting.
   */

  #if defined(ITM2STM)
    // we're using RSTM, so setting and getting names of policies and
    // algorithms matters.  The macros will interface directly with the
    // library
    #define  TM_SET_POLICY(P)              stm::set_policy(P)
    #define  TM_GET_ALGNAME()              stm::get_algname()

  #elif defined(ITM) // [mfs] should this be an else as below?
    // we're not using RSTM, so the only thing to do is return the fact that
    // ITM is in use
    #define  TM_SET_POLICY(P)
    #define  TM_GET_ALGNAME()              "icc builtin libitm.a"
  #endif

  // the library API has some hacks for nontransactional initialization.  They
  // have no meaning when using compiler-based instrumentation.
  #define  TM_BEGIN_FAST_INITIALIZATION  nop
  #define  TM_END_FAST_INITIALIZATION    nop

/**
 *  Definitions for use with the Oracle TM compiler
 */
#elif defined(STM_CC_SUN)

  // needed for jmp_buf and setjmp
  #include <setjmp.h>

  #if defined(OTM2STM)
  // for getting inlining to work
  #include "libotm2stm/OTM_Inliner.i"
  #endif

  // needed for scope_t and sys_shutdown
  namespace stm
  {
    typedef void scope_t;
    void sys_shutdown();
  }

  // marker for a function that can be called from a transaction
  #define TM_CALLABLE         [[transaction_safe]]

  #if defined(OTM2STM)

    // prototype for the special function we use to push the buffer into the
    // descriptor
    void OTM_PREBEGIN(stm::scope_t*);

    // markers for the beginning and end of a transaction when using RSTM
    #define TM_BEGIN(TYPE)                            \
      {                                               \
          jmp_buf _jmpbuf;                            \
          setjmp(_jmpbuf);                            \
          OTM_PREBEGIN((stm::scope_t*)&_jmpbuf);      \
          __transaction [[TYPE]] {

    #define TM_END                                    \
                                 }                    \
      }
  #else // [mfs] should this be an elif
    // markers for the beginning and end of a trnasaction when using SkySTM
    #define TM_BEGIN(TYPE)         __transaction [[TYPE]] {
    #define TM_END                 }
  #endif

  // marker for a nontransactional region within a transaction
  #define TM_WAIVER           __transaction [[waiver]]

  // all descriptor-management stuff is meaningless when there is a compiler
  // transforming the code
  #define TM_GET_THREAD()
  #define TM_ARG
  #define TM_ARG_ALONE
  #define TM_PARAM
  #define TM_PARAM_ALONE

  // reads and writes just transform to naked reads and writes, since the
  // compiler will transform them after the macro preprocessor step
  #define TM_READ(x) (x)
  #define TM_WRITE(x, y) (x) = (y)

  // initialization and shutdown routines have no meaning
  #define  TM_SYS_INIT                   nop
  #define  TM_THREAD_INIT                nop
  #define  TM_THREAD_SHUTDOWN            nop

  // memory management
  #define  TM_ALLOC                      malloc
  #define  TM_FREE                       free

  /**
   *  Custom RSTM stuff.  We support using RSTM or SkySTM (the Oracle TM
   *  default), so we have to do the same sort of stuff as for ICC
   */
  #if defined(OTM2STM)
    // we're using RSTM, so interface with the library
    #define  TM_SET_POLICY(P)              stm::set_policy(P)
    #define  TM_GET_ALGNAME()              stm::get_algname()
    #define  TM_SYS_SHUTDOWN               stm::sys_shutdown
  #else // [mfs] should this be an elif as above?
    // we're not using RSTM, so return the fact that SkySTM is in use
    #define  TM_SET_POLICY(P)
    #define  TM_GET_ALGNAME()              "SunCC builtin libSkySTMLib.a"
    #define  TM_SYS_SHUTDOWN               nop
  #endif

  // the library API has some hacks for nontransactional initialization.  They
  // have no meaning when using compiler-based instrumentation
  #define  TM_BEGIN_FAST_INITIALIZATION  nop
  #define  TM_END_FAST_INITIALIZATION    nop

/**
 *  If neither SunCC or ICC is in use, we can't continue with the build
 */
#else
  #error "We're not prepared for your implementation of the C++ TM spec."
#endif

#endif // API_CXXTM_HPP
