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
 *  This file declares the methods that install a new algorithm
 *
 *  [mfs] This file does a whole lot more than just install algorithms now.
 *        And it needs to do even more.  My goal is for "inst" to describe
 *        everything there is to describe about instrumentation.  That is, it
 *        should address whether the API has funciton pointers or now,
 *        whether those function pointers are per-thread or not,
 *        irrevocability, and the registration of algorithm implementations
 *        when adaptivity is in use.
 *
 *  [mfs] I think we want to support the following modes:
 *        - per-thread function pointers w/ adaptivity
 *        - global function pointers w/ adaptivity
 *        - static functions w/ adaptivity (via a switch statement?)
 *        - static functions w/o adaptivity
 *
 *  [mfs] This suggests that we should make adaptivity and the access of
 *        instrumentation orthogonal.  Can we?
 */

#ifndef INST_HPP__
#define INST_HPP__

#include "../include/ThreadLocal.hpp"
#include "../include/abstract_compiler.hpp"
#include "../include/tlsapi.hpp"
#include "../include/macros.hpp"
#include "txthread.hpp"

namespace stm
{
#if defined(STM_INST_COARSEGRAINADAPT) || defined(STM_INST_SWITCHADAPT) || defined(STM_INST_ONESHOT)
  const uint32_t MODE_TURBO    = 2;
  const uint32_t MODE_WRITE    = 1;
  const uint32_t MODE_RO       = 0;
#elif defined(STM_INST_FINEGRAINADAPT)
#else
#error "Unable to determine Instrumentation mode"
#endif

  /**
   * custom begin method that blocks the starting thread, in order to get
   * rendezvous correct during mode switching and GRL irrevocability
   * (implemented in irrevocability.cpp because it uses some static functions
   * declared there)
   */
  void begin_blocker(TX_LONE_PARAMETER);

  /*** POINTERS TO INSTRUMENTATION */

#if defined(STM_INST_FINEGRAINADAPT)
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
#if defined(STM_TLS_PTHREAD)
  extern THREAD_LOCAL_FUNCTION_DECL_TYPE(TM_FASTCALL void(*)(TX_LONE_PARAMETER), tmcommit);
  extern THREAD_LOCAL_FUNCTION_DECL_TYPE(TM_FASTCALL void*(*)(TX_FIRST_PARAMETER STM_READ_SIG(,)), tmread);
  extern THREAD_LOCAL_FUNCTION_DECL_TYPE(TM_FASTCALL void(*)(TX_FIRST_PARAMETER STM_WRITE_SIG(,,)), tmwrite);
#else
  extern THREAD_LOCAL_DECL_TYPE(TM_FASTCALL void(*tmcommit)(TX_LONE_PARAMETER));
  extern THREAD_LOCAL_DECL_TYPE(TM_FASTCALL void*(*tmread)(TX_FIRST_PARAMETER STM_READ_SIG(,)));
  extern THREAD_LOCAL_DECL_TYPE(TM_FASTCALL void(*tmwrite)(TX_FIRST_PARAMETER STM_WRITE_SIG(,,)));
#endif

  /*** Global pointer for switching to irrevocable mode */
  extern bool(*tmirrevoc)(TxThread*);

  /*** Global pointer for how to rollback */
  extern void (*tmrollback)(STM_ROLLBACK_SIG(,,));

  /**
   * The global pointer for starting transactions. The return value should
   * be true if the transaction was started as irrevocable, the caller can
   * use this return to execute completely uninstrumented code if it's
   * available.
   */
  extern void(*volatile tmbegin)(TX_LONE_PARAMETER);

#elif defined(STM_INST_COARSEGRAINADAPT)
  /**
   *  Like above, except that now function pointers aren't per-thread
   */
  extern TM_FASTCALL void(*tmcommit)(TX_LONE_PARAMETER);
  extern TM_FASTCALL void*(*tmread)(TX_FIRST_PARAMETER STM_READ_SIG(,));
  extern TM_FASTCALL void(*tmwrite)(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));

  /*** Global pointer for switching to irrevocable mode */
  extern bool(*tmirrevoc)(TxThread*);

  /*** Global pointer for how to rollback */
  extern void (*tmrollback)(STM_ROLLBACK_SIG(,,));

  /**
   * The global pointer for starting transactions. The return value should
   * be true if the transaction was started as irrevocable, the caller can
   * use this return to execute completely uninstrumented code if it's
   * available.
   */
  extern void(*volatile tmbegin)(TX_LONE_PARAMETER);

#elif defined(STM_INST_SWITCHADAPT) || defined(STM_INST_ONESHOT)
  /**
   * And now we actually have static functions, rather than pointers
   */
  TM_FASTCALL void  tmcommit(TX_LONE_PARAMETER);
  TM_FASTCALL void* tmread(TX_FIRST_PARAMETER STM_READ_SIG(,));
  TM_FASTCALL void  tmwrite(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));

  bool tmirrevoc(TxThread*);
  void tmbegin(TX_LONE_PARAMETER);

#if defined(STM_INST_SWITCHADAPT)
  /*** Global pointer for how to rollback */
  extern void (*tmrollback)(STM_ROLLBACK_SIG(,,));
#else // defined(STM_INST_ONESHOT)
  void tmrollback(STM_ROLLBACK_SIG(,,));
#endif

#else
#error "Unable to determine Instrumentation mode"
#endif

  /**
   * The function for aborting a transaction.  This contains all of the
   * generic rollback code, and calls out to tmrollback for
   * algorithm-specific unwinding
   */
  NORETURN void tmabort();

  /*** actually make all threads use the new algorithm */
  void install_algorithm(int new_alg, TxThread* tx);

  /*** make just this thread use a new algorith (use in ctors) */
  void install_algorithm_local(int new_alg);

  // CGL is the default algorithm, and it is useful to declare this somewhere
  //
  // [mfs] But this isn't the right place!
  void CGLBegin(TX_LONE_PARAMETER);

  typedef TM_FASTCALL void* (*ReadBarrier)(TX_FIRST_PARAMETER STM_READ_SIG(,));
  typedef TM_FASTCALL void (*WriteBarrier)(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  typedef TM_FASTCALL void (*CommitBarrier)(TX_LONE_PARAMETER );

  /**
   *  Configure the fields that a thread uses for tracking its read/write
   *  mode
   */
  inline void initializeThreadInst(TxThread* tx)
  {
#if defined(STM_INST_FINEGRAINADAPT)
      // set my pointers
      tx->my_tmread = (void**)&tmread;
      tx->my_tmwrite = (void**)&tmwrite;
      tx->my_tmcommit = (void**)&tmcommit;
#elif defined(STM_INST_COARSEGRAINADAPT) || defined(STM_INST_SWITCHADAPT) || defined(STM_INST_ONESHOT)
      tx->mode = MODE_RO;  // the default
#else
#error "Unable to determine Instrumentation mode"
#endif
  }

  /**
   *  Mode-switching codes... these all get inlined, so the extra parameters
   *  shouldn't be a problem
   *
   *  [mfs] should we move this to another file?
   */

#if defined(STM_INST_FINEGRAINADAPT)
  inline void SetLocalPointers(ReadBarrier r, WriteBarrier w, CommitBarrier c)
  {
      tmread = r;
      tmwrite = w;
      tmcommit = c;
  }

  inline
  void ResetToRO(TxThread*, ReadBarrier r, WriteBarrier w, CommitBarrier c)
  {
      SetLocalPointers(r, w, c);
  }
  inline
  void OnFirstWrite(TxThread*, ReadBarrier r, WriteBarrier w, CommitBarrier c)
  {
      SetLocalPointers(r, w, c);
  }
  inline void GoTurbo(TxThread*, ReadBarrier r, WriteBarrier w, CommitBarrier c)
  {
      SetLocalPointers(r, w, c);
  }
  inline bool CheckTurboMode(TxThread*, ReadBarrier r)
  {
      return (tmread == r);
  }
  inline bool CheckROMode(TxThread*, ReadBarrier r)
  {
      return (tmread == r);
  }
#elif defined(STM_INST_COARSEGRAINADAPT) || defined(STM_INST_SWITCHADAPT) || defined(STM_INST_ONESHOT)
  inline
  void ResetToRO(TxThread* tx, ReadBarrier, WriteBarrier, CommitBarrier)
  {
      tx->mode = MODE_RO;
  }
  inline
  void OnFirstWrite(TxThread* tx, ReadBarrier, WriteBarrier, CommitBarrier)
  {
      tx->mode = MODE_WRITE;
  }
  inline void GoTurbo(TxThread* tx, ReadBarrier, WriteBarrier, CommitBarrier)
  {
      tx->mode = MODE_TURBO;
  }
  inline bool CheckTurboMode(TxThread* tx, ReadBarrier)
  {
      return tx->mode == MODE_TURBO;
  }
  inline bool CheckROMode(TxThread* tx, ReadBarrier)
  {
      return tx->mode == MODE_RO;
  }
#else
#error "Unable to determine Instrumentation mode"
#endif
} // namespace stm

#if defined(STM_INST_FINEGRAINADAPT)

// NB: in FINEGRAINADAPT mode, we don't need these functions, so we don't
// create them...
#define DECLARE_SIMPLE_METHODS_FROM_SIMPLE_TEMPLATE(TCLASS, CLASS, TEMPLATE)
#define DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(TCLASS, CLASS, TEMPLATE)
#define DECLARE_SIMPLE_METHODS_FROM_NORMAL(CLASS)
#define DECLARE_SIMPLE_METHODS_FROM_TURBO(CLASS)

#else

// if an algorithm is defined as having Turbo, RO, and RW modes, then we
// can use this to create generic Read/Write/Commit functions
#define DECLARE_SIMPLE_METHODS_FROM_TURBO(CLASS)                        \
namespace stm                                                           \
{                                                                       \
    TM_FASTCALL void* CLASS##Read(TX_FIRST_PARAMETER                    \
                                  STM_READ_SIG(addr,))                  \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (CheckTurboMode(tx, CLASS##ReadTurbo))                       \
            return CLASS##ReadTurbo(TX_FIRST_ARG addr STM_MASK(mask));  \
        else if (!CheckROMode(tx, CLASS##ReadRO))                       \
            return CLASS##ReadRW(TX_FIRST_ARG addr STM_MASK(mask));     \
        else                                                            \
            return CLASS##ReadRO(TX_FIRST_ARG addr STM_MASK(mask));     \
    }                                                                   \
    TM_FASTCALL void CLASS##Write(TX_FIRST_PARAMETER                    \
                                  STM_WRITE_SIG(addr,value,mask))       \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (CheckTurboMode(tx, CLASS##ReadTurbo))                       \
            CLASS##WriteTurbo(TX_FIRST_ARG addr,                        \
                              value STM_MASK(mask));                    \
        else if (!CheckROMode(tx, CLASS##ReadRO))                       \
            CLASS##WriteRW(TX_FIRST_ARG addr, value STM_MASK(mask));    \
        else                                                            \
            CLASS##WriteRO(TX_FIRST_ARG addr, value STM_MASK(mask));    \
    }                                                                   \
    TM_FASTCALL void CLASS##Commit(TX_LONE_PARAMETER)                   \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (CheckTurboMode(tx, CLASS##ReadTurbo))                       \
            CLASS##CommitTurbo(TX_LONE_ARG);                            \
        else if (!CheckROMode(tx, CLASS##ReadRO))                       \
            CLASS##CommitRW(TX_LONE_ARG);                               \
        else                                                            \
            CLASS##CommitRO(TX_LONE_ARG);                               \
    }                                                                   \
}

// if an algorithm is defined as having RO and RW modes, then we
// can use this to create generic Read/Write/Commit functions
#define DECLARE_SIMPLE_METHODS_FROM_NORMAL(CLASS)                       \
namespace stm                                                           \
{                                                                       \
    TM_FASTCALL void* CLASS##Read(TX_FIRST_PARAMETER                    \
                                  STM_READ_SIG(addr,))                  \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (!CheckROMode(tx, CLASS##ReadRO))                            \
            return CLASS##ReadRW(TX_FIRST_ARG addr STM_MASK(mask));     \
        else                                                            \
            return CLASS##ReadRO(TX_FIRST_ARG addr STM_MASK(mask));     \
    }                                                                   \
    TM_FASTCALL void CLASS##Write(TX_FIRST_PARAMETER                    \
                                  STM_WRITE_SIG(addr,value,mask))       \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (!CheckROMode(tx, CLASS##ReadRO))                            \
            CLASS##WriteRW(TX_FIRST_ARG addr, value STM_MASK(mask));    \
        else                                                            \
            CLASS##WriteRO(TX_FIRST_ARG addr, value STM_MASK(mask));    \
    }                                                                   \
    TM_FASTCALL void CLASS##Commit(TX_LONE_PARAMETER)                   \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (!CheckROMode(tx, CLASS##ReadRO))                            \
            CLASS##CommitRW(TX_LONE_ARG);                               \
        else                                                            \
            CLASS##CommitRO(TX_LONE_ARG);                               \
    }                                                                   \
}

// if an algorithm is templated and is defined as having RO and RW modes,
// then we can use this to create generic Read/Write/Commit functions
#define DECLARE_SIMPLE_METHODS_FROM_TEMPLATE(TCLASS, CLASS, TEMPLATE)   \
namespace stm                                                           \
{                                                                       \
    TM_FASTCALL void* CLASS##Read(TX_FIRST_PARAMETER                    \
                                  STM_READ_SIG(addr,))                  \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (!CheckROMode(tx, TCLASS##GenericReadRO<TEMPLATE>))          \
            return TCLASS##GenericReadRW<TEMPLATE>(TX_FIRST_ARG addr    \
                                                   STM_MASK(mask));     \
        else                                                            \
            return TCLASS##GenericReadRO<TEMPLATE>(TX_FIRST_ARG addr    \
                                                   STM_MASK(mask));     \
    }                                                                   \
    TM_FASTCALL void CLASS##Write(TX_FIRST_PARAMETER                    \
                                  STM_WRITE_SIG(addr,value,mask))       \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (!CheckROMode(tx, TCLASS##GenericReadRO<TEMPLATE>))          \
            TCLASS##GenericWriteRW<TEMPLATE>(TX_FIRST_ARG addr, value   \
                                             STM_MASK(mask));           \
        else                                                            \
            TCLASS##GenericWriteRO<TEMPLATE>(TX_FIRST_ARG addr, value   \
                                             STM_MASK(mask));           \
    }                                                                   \
    TM_FASTCALL void CLASS##Commit(TX_LONE_PARAMETER)                   \
    {                                                                   \
        TX_GET_TX_INTERNAL;                                             \
        if (!CheckROMode(tx, TCLASS##GenericReadRO<TEMPLATE>))          \
            TCLASS##GenericCommitRW<TEMPLATE>(TX_LONE_ARG);             \
        else                                                            \
            TCLASS##GenericCommitRO<TEMPLATE>(TX_LONE_ARG);             \
    }                                                                   \
    void CLASS##Rollback(STM_ROLLBACK_SIG(tx,,))                        \
    {                                                                   \
        TCLASS##GenericRollback<TEMPLATE>(tx);                          \
    }                                                                   \
    bool CLASS##Irrevoc(TxThread* tx)                                   \
    {                                                                   \
        return TCLASS##GenericIrrevoc<TEMPLATE>(tx);                    \
    }                                                                   \
    void CLASS##OnSwitchTo()                                            \
    {                                                                   \
        TCLASS##GenericOnSwitchTo<TEMPLATE>();                          \
    }                                                                   \
    void CLASS##Begin(TX_LONE_PARAMETER)                                \
    {                                                                   \
        TCLASS##GenericBegin<TEMPLATE>(TX_LONE_ARG);                    \
    }                                                                   \
}

// if an algorithm is templated but doesn't have RO/RW modes, then we can
// use this to create generic Read/Write/Commit functions
#define DECLARE_SIMPLE_METHODS_FROM_SIMPLE_TEMPLATE(TCLASS, CLASS, TEMPLATE) \
namespace stm                                                           \
{                                                                       \
    TM_FASTCALL void* CLASS##Read(TX_FIRST_PARAMETER                    \
                                  STM_READ_SIG(addr,))                  \
    {                                                                   \
        return TCLASS##GenericRead<TEMPLATE>(TX_FIRST_ARG addr          \
                                             STM_MASK(mask));           \
    }                                                                   \
    TM_FASTCALL void CLASS##Write(TX_FIRST_PARAMETER                    \
                                  STM_WRITE_SIG(addr,value,mask))       \
    {                                                                   \
        TCLASS##GenericWrite<TEMPLATE>(TX_FIRST_ARG addr, value         \
                                       STM_MASK(mask));                 \
    }                                                                   \
    TM_FASTCALL void CLASS##Commit(TX_LONE_PARAMETER)                   \
    {                                                                   \
        TCLASS##GenericCommit<TEMPLATE>(TX_LONE_ARG);                   \
    }                                                                   \
    void CLASS##Rollback(STM_ROLLBACK_SIG(tx,,))                        \
    {                                                                   \
        TCLASS##GenericRollback<TEMPLATE>(tx);                          \
    }                                                                   \
    bool CLASS##Irrevoc(TxThread* tx)                                   \
    {                                                                   \
        return TCLASS##GenericIrrevoc<TEMPLATE>(tx);                    \
    }                                                                   \
    void CLASS##OnSwitchTo()                                            \
    {                                                                   \
        TCLASS##GenericOnSwitchTo<TEMPLATE>();                          \
    }                                                                   \
    void CLASS##Begin(TX_LONE_PARAMETER)                                \
    {                                                                   \
        TCLASS##GenericBegin<TEMPLATE>(TX_LONE_ARG);                    \
    }                                                                   \
}

#endif

// now we need to define how an algorithm gets registered
//
// [mfs] Ideally, this would evaluate to a NOP if STM_ONESHOT_MODE was on...
#if defined(STM_INST_FINEGRAINADAPT)

# define REGISTER_FGADAPT_ALG(TOKEN, NAME, PRIV)        \
    namespace stm                                       \
    {                                                   \
        template<>                                      \
        void registerTM<TOKEN>()                        \
        {                                               \
            stms[TOKEN].name = NAME;                    \
            stms[TOKEN].begin     = TOKEN##Begin;       \
            stms[TOKEN].commit    = TOKEN##CommitRO;    \
            stms[TOKEN].read      = TOKEN##ReadRO;      \
            stms[TOKEN].write     = TOKEN##WriteRO;     \
            stms[TOKEN].rollback  = TOKEN##Rollback;    \
            stms[TOKEN].irrevoc   = TOKEN##Irrevoc;     \
            stms[TOKEN].switcher  = TOKEN##OnSwitchTo;  \
            stms[TOKEN].privatization_safe = PRIV;      \
        }                                               \
    }

# define REGISTER_TEMPLATE_ALG(TCLASS, TOKEN, NAME, PRIV, TEMPLATE)     \
    namespace stm                                                       \
    {                                                                   \
        template<>                                                      \
        void registerTM<TOKEN>()                                        \
        {                                                               \
            stms[TOKEN].name = NAME;                                    \
            stms[TOKEN].begin     = TCLASS##GenericBegin<TEMPLATE>;     \
            stms[TOKEN].commit    = TCLASS##GenericCommitRO<TEMPLATE>;  \
            stms[TOKEN].read      = TCLASS##GenericReadRO<TEMPLATE>;    \
            stms[TOKEN].write     = TCLASS##GenericWriteRO<TEMPLATE>;   \
            stms[TOKEN].rollback  = TCLASS##GenericRollback<TEMPLATE>;  \
            stms[TOKEN].irrevoc   = TCLASS##GenericIrrevoc<TEMPLATE>;   \
            stms[TOKEN].switcher  = TCLASS##GenericOnSwitchTo<TEMPLATE>; \
            stms[TOKEN].privatization_safe = PRIV;                      \
        }                                                               \
    }
# define REGISTER_SIMPLE_TEMPLATE_ALG(TCLASS, TOKEN, NAME, PRIV, TEMPLATE) \
    namespace stm                                                       \
    {                                                                   \
        template<>                                                      \
        void registerTM<TOKEN>()                                        \
        {                                                               \
            stms[TOKEN].name = NAME;                                    \
            stms[TOKEN].begin     = TCLASS##GenericBegin<TEMPLATE>;     \
            stms[TOKEN].commit    = TCLASS##GenericCommit<TEMPLATE>;    \
            stms[TOKEN].read      = TCLASS##GenericRead<TEMPLATE>;      \
            stms[TOKEN].write     = TCLASS##GenericWrite<TEMPLATE>;     \
            stms[TOKEN].rollback  = TCLASS##GenericRollback<TEMPLATE>;  \
            stms[TOKEN].irrevoc   = TCLASS##GenericIrrevoc<TEMPLATE>;   \
            stms[TOKEN].switcher  = TCLASS##GenericOnSwitchTo<TEMPLATE>; \
            stms[TOKEN].privatization_safe = PRIV;                      \
        }                                                               \
    }

# define REGISTER_REGULAR_ALG(TOKEN, NAME, PRIV)        \
    namespace stm                                       \
    {                                                   \
        template<>                                      \
        void registerTM<TOKEN>()                        \
        {                                               \
            stms[TOKEN].name = NAME;                    \
            stms[TOKEN].begin     = TOKEN##Begin;       \
            stms[TOKEN].commit    = TOKEN##Commit;      \
            stms[TOKEN].read      = TOKEN##Read;        \
            stms[TOKEN].write     = TOKEN##Write;       \
            stms[TOKEN].rollback  = TOKEN##Rollback;    \
            stms[TOKEN].irrevoc   = TOKEN##Irrevoc;     \
            stms[TOKEN].switcher  = TOKEN##OnSwitchTo;  \
            stms[TOKEN].privatization_safe = PRIV;      \
        }                                               \
    }
#elif defined(STM_INST_COARSEGRAINADAPT)

# define REGISTER_REGULAR_ALG(TOKEN, NAME, PRIV)        \
    namespace stm                                       \
    {                                                   \
        template<>                                      \
        void registerTM<TOKEN>()                        \
        {                                               \
            stms[TOKEN].name      = NAME;               \
            stms[TOKEN].begin     = TOKEN##Begin;       \
            stms[TOKEN].commit    = TOKEN##Commit;      \
            stms[TOKEN].read      = TOKEN##Read;        \
            stms[TOKEN].write     = TOKEN##Write;       \
            stms[TOKEN].rollback  = TOKEN##Rollback;    \
            stms[TOKEN].irrevoc   = TOKEN##Irrevoc;     \
            stms[TOKEN].switcher  = TOKEN##OnSwitchTo;  \
            stms[TOKEN].privatization_safe = PRIV;      \
        }                                               \
    }

# define REGISTER_TEMPLATE_ALG(TCLASS, TOKEN, NAME, PRIV, TEMPLATE) \
    REGISTER_REGULAR_ALG(TOKEN, NAME, PRIV)

# define REGISTER_FGADAPT_ALG(TOKEN, NAME, PRIV)    \
    REGISTER_REGULAR_ALG(TOKEN, NAME, PRIV)

# define REGISTER_SIMPLE_TEMPLATE_ALG(TCLASS, TOKEN, NAME, PRIV, TEMPLATE) \
    REGISTER_REGULAR_ALG(TOKEN, NAME, PRIV)

#elif defined(STM_INST_SWITCHADAPT) || defined(STM_INST_ONESHOT)
// [mfs] Registration for SWITCH and ONESHOT is going to use the table for
// now, so that we don't have to manually generate quite so many different
// TOKEN##Name functions
# define REGISTER_REGULAR_ALG(TOKEN, NAME, PRIV)        \
    namespace stm                                       \
    {                                                   \
        template<>                                      \
        void registerTM<TOKEN>()                        \
        {                                               \
            stms[TOKEN].name      = NAME;               \
            stms[TOKEN].rollback  = TOKEN##Rollback;    \
            stms[TOKEN].switcher  = TOKEN##OnSwitchTo;  \
            stms[TOKEN].privatization_safe = PRIV;      \
        }                                               \
    }

# define REGISTER_TEMPLATE_ALG(TCLASS, TOKEN, NAME, PRIV, TEMPLATE) \
    REGISTER_REGULAR_ALG(TOKEN, NAME, PRIV)

# define REGISTER_FGADAPT_ALG(TOKEN, NAME, PRIV)    \
    REGISTER_REGULAR_ALG(TOKEN, NAME, PRIV)

# define REGISTER_SIMPLE_TEMPLATE_ALG(TCLASS, TOKEN, NAME, PRIV, TEMPLATE) \
    REGISTER_REGULAR_ALG(TOKEN, NAME, PRIV)

#else
#  error "Invalid configuration option"
#endif


// now we need to indicate how ONESHOT will handle renaming from CLASS##XYZ
// to tmxyz.  Note that this assumes that DECLARE_SIMPLE_METHODS_XXX has been
// used already
//
// [mfs] It would be great if this didn't have to be called from within an
//       #ifdef, but I don't know how to achieve that...
#if defined(STM_INST_ONESHOT)
#define DECLARE_AS_ONESHOT(CLASS)                                       \
    namespace stm                                                       \
    {                                                                   \
        void tmbegin(TX_LONE_PARAMETER)                                 \
        {                                                               \
            CLASS##Begin(TX_LONE_ARG);                                  \
        }                                                               \
        TM_FASTCALL void* tmread(TX_FIRST_PARAMETER STM_READ_SIG(addr,)) \
        {                                                               \
            return CLASS##Read(TX_FIRST_ARG addr STM_MASK(mask));       \
        }                                                               \
        TM_FASTCALL void tmwrite(TX_FIRST_PARAMETER                     \
                                 STM_WRITE_SIG(addr,value,mask))        \
        {                                                               \
            CLASS##Write(TX_FIRST_ARG addr, value STM_MASK(mask));      \
        }                                                               \
        TM_FASTCALL void tmcommit(TX_LONE_PARAMETER)                    \
        {                                                               \
            CLASS##Commit(TX_LONE_ARG);                                 \
        }                                                               \
        bool tmirrevoc(TxThread* tx)                                    \
        {                                                               \
            return CLASS##Irrevoc(tx);                                  \
        }                                                               \
        void tmrollback(STM_ROLLBACK_SIG(tx,,))                         \
        {                                                               \
            CLASS##Rollback(tx);                                        \
        }                                                               \
    }
#elif defined(STM_INST_FINEGRAINADAPT) || defined(STM_INST_COARSEGRAINADAPT) || defined(STM_INST_SWITCHADAPT)
#define DECLARE_AS_ONESHOT(CLASS) "ERROR: You should not use DECLARE_AS_ONESHOT unless STM_INST_ONESHOT is defined"
#else
#error "Unable to determine Instrumentation mode"
#endif

#endif // INST_HPP__
