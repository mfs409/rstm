/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "adaptivity.hpp"

namespace stm
{
  struct alg_t
  {
      int identifier;
      void (*tm_begin)(scope_t*);
      void (*tm_end)();
      void* (* TM_FASTCALL tm_read)(void**);
      void (* TM_FASTCALL tm_write)(void**, void*);
      scope_t* (*rollback)(TX*);
      const char* (*tm_getalgname)();
      void* (*tm_alloc)(size_t);
      void (*tm_free)(void*);

      // [TODO]
      // bool (* irrevoc)(TxThread*);
      // void (* switcher) ();
      // bool privatization_safe;
  };

  alg_t tm_info[TM_NAMES_MAX];

  void registerTMAlg(int identifier,
                     void (*tm_begin)(scope_t*),
                     void (*tm_end)(),
                     void* (* TM_FASTCALL tm_read)(void**),
                     void (* TM_FASTCALL tm_write)(void**, void*),
                     scope_t* (*rollback)(TX*),
                     const char* (*tm_getalgname)(),
                     void* (*tm_alloc)(size_t),
                     void (*tm_free)(void*))
  {
      tm_info[identifier].tm_begin = tm_begin;
      tm_info[identifier].tm_end = tm_end;
      tm_info[identifier].tm_read = tm_read;
      tm_info[identifier].tm_write = tm_write;
      tm_info[identifier].rollback = rollback;
      tm_info[identifier].tm_getalgname = tm_getalgname;
      tm_info[identifier].tm_alloc = tm_alloc;
      tm_info[identifier].tm_free = tm_free;
  }




  /**
   *  Template Metaprogramming trick for initializing all STM algorithms.
   *
   *  This is either a very gross trick, or a very cool one.  We have ALG_MAX
   *  algorithms, and they all need to be initialized.  Each has a unique
   *  identifying integer, and each is initialized by calling an instantiation
   *  of initTM<> with that integer.
   *
   *  Rather than call each function through a line of code, we use a
   *  tail-recursive template: When we call MetaInitializer<0>.init(), it will
   *  recursively call itself for every X, where 0 <= X < ALG_MAX.  Since
   *  MetaInitializer<X>::init() calls initTM<X> before recursing, this
   *  instantiates and calls the appropriate initTM function.  Thus we
   *  correctly call all initialization functions.
   *
   *  Furthermore, since the code is tail-recursive, at -O3 g++ will inline all
   *  the initTM calls right into the sys_init function.  While the code is not
   *  performance critical, it's still nice to avoid the overhead.
   */
  template <int I = 0>
  struct MetaInitializer
  {
      /*** default case: init the Ith tm, then recurse to I+1 */
      static void init()
      {
          initTM<(TM_NAMES)I>();
          MetaInitializer<(stm::TM_NAMES)I+1>::init();
      }
  };
  template <>
  struct MetaInitializer<TM_NAMES_MAX>
  {
      /*** termination case: do nothing for TM_NAMES_MAX */
      static void init() { }
  };

  #if 0
  /**
   *  Initialize the TM system.
   */
  void sys_init()
  {
      static volatile uint32_t mtx = 0;

      if (bcas32(&mtx, 0u, 1u)) {
          // manually register all behavior policies that we support.  We do
          // this via tail-recursive template metaprogramming

          // [mfs] changed here
          MetaInitializer<0>::init();

          // guess a default configuration, then check env for a better option
          const char* cfg = "NOrec";
          const char* configstring = getenv("STM_CONFIG");
          if (configstring)
              cfg = configstring;
          else
              printf("STM_CONFIG environment variable not found... using %s\n", cfg);
          init_lib_name = cfg;

          // now initialize the the adaptive policies
          pol_init(cfg);

          // this is (for now) how we make sure we have a buffer to hold
          // profiles.  This also specifies how many profiles we do at a time.
          char* spc = getenv("STM_NUMPROFILES");
          if (spc != NULL)
              profile_txns = strtol(spc, 0, 10);
          profiles = (dynprof_t*)malloc(profile_txns * sizeof(dynprof_t));
          for (unsigned i = 0; i < profile_txns; i++)
              profiles[i].clear();

          // Initialize the global abort handler.
          if (conflict_abort_handler)
              TxThread::tmabort = conflict_abort_handler;

          // now set the phase
          set_policy(cfg);

          printf("STM library configured using config == %s\n", cfg);

          mtx = 2;
      }
      while (mtx != 2) { }
  }
#endif
}
