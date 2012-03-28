/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <stdint.h>
#include <iostream>
#include <cstdlib>
#include <setjmp.h> // factor this out into the API?
#include "tx.hpp"
#include "platform.hpp"
#include "locks.hpp"
#include "metadata.hpp"
#include "adaptivity.hpp"

/**
 * This STM is implemented differently than all others.  We don't give it a
 * custom namespace, and we don't rely on tx.cpp.  Instead, we implement
 * everything directly within this file.  In that way, we can get all of our
 * adaptivity hooks to work correctly.
 *
 * NB: right now, we pick an algorithm at begin time, but we don't actually
 *     adapt yet
 */

namespace stm
{
  /**
   *  When the transactional system gets shut down, we call this to dump
   *  stats for all threads
   */
  void tm_sys_shutdown()
  {
      static volatile unsigned int mtx = 0;
      // while (!bcas32(&mtx, 0u, 1u)) { }
      for (uint32_t i = 0; i < threadcount.val; i++) {
          std::cout << "Thread: "       << threads[i]->id
                    << "; RO Commits: " << threads[i]->commits_ro
                    << "; RW Commits: " << threads[i]->commits_rw
                    << "; Aborts: "     << threads[i]->aborts
                    << std::endl;
      }
      CFENCE;
      mtx = 0;
  }

  /**
   *  To initialize the thread's TM support, we need only ensure it has a
   *  descriptor.
   */
  void tm_thread_init()
  {
      // multiple inits from one thread do not cause trouble
      if (Self) return;

      // create a TxThread and save it in thread-local storage
      Self = new TX();
  }

  /**
   *  When a thread is done using the TM, we don't need to do anything
   *  special.
   */
  void tm_thread_shutdown() { }

  /**
   *  All behaviors are reached via function pointers.  This allows us to
   *  change on the fly:
   */
  static rollback_t        rollback_;
  static tm_begin_t        tm_begin_;
  static tm_end_t          tm_end_;
  static tm_get_alg_name_t tm_getalgname_;
  static tm_alloc_t        tm_alloc_;
  static tm_free_t         tm_free_;
  static tm_read_t         tm_read_;
  static tm_write_t        tm_write_;

  /**
   *  The default mechanism that libstm uses for an abort. An API environment
   *  may also provide its own abort mechanism (see itm2stm for an example of
   *  how the itm shim does this).
   *
   *  This is ugly because rollback has a configuration-dependent signature.
   */
  NOINLINE
  NORETURN
  void tm_abort(TX* tx)
  {
      jmp_buf* scope = (jmp_buf*)rollback_(tx);
      // need to null out the scope
      longjmp(*scope, 1);
  }

  // for CM
  pad_word_t fcm_timestamp = {0};
  pad_word_t epochs[MAX_THREADS] = {{0}};

  // forward all calls to the function pointers
  void tm_begin(void* buf) { tm_begin_(buf); }
  void tm_end() { tm_end_(); }
  void* tm_alloc(size_t s) { return tm_alloc_(s); }
  void tm_free(void* p) { tm_free_(p); }
  TM_FASTCALL
  void* tm_read(void** addr) { return tm_read_(addr); }
  TM_FASTCALL
  void tm_write(void** addr, void* val) { tm_write_(addr, val); }

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

  /**
   *  Initialize all of the TM algorithms
   */
  void tm_sys_init()
  {
      // manually register all behavior policies that we support.  We do
      // this via tail-recursive template metaprogramming
      MetaInitializer<0>::init();

      // guess a default configuration, then check env for a better option
      const char* cfg = "NOrec";
      const char* configstring = getenv("STM_CONFIG");
      if (configstring)
          cfg = configstring;
      else
          printf("STM_CONFIG environment variable not found... using %s\n", cfg);

      bool found = false;
      for (int i = 0; i < TM_NAMES_MAX; ++i) {
          const char* name = tm_info[i].tm_getalgname();
          if (0 == strcmp(cfg, name)) {
              rollback_ = tm_info[i].rollback;
              tm_begin_ = tm_info[i].tm_begin;
              tm_end_ = tm_info[i].tm_end;
              tm_getalgname_ = tm_info[i].tm_getalgname;
              tm_alloc_ = tm_info[i].tm_alloc;
              tm_free_ = tm_info[i].tm_free;
              tm_read_ = tm_info[i].tm_read;
              tm_write_ = tm_info[i].tm_write;
              found = true;
              break;
          }
      }
      printf("STM library configured using config == %s\n", cfg);
  }

  char* trueAlgName = NULL;
  const char* tm_getalgname()
  {
      if (trueAlgName)
          return trueAlgName;

      const char* s1 = "AdapTM";
      const char* s2 = tm_getalgname_();
      size_t l1 = strlen(s1);
      size_t l2 = strlen(s2);
      trueAlgName = (char*)malloc((l1+l2+3)*sizeof(char));
      strcpy(trueAlgName, s1);
      trueAlgName[l1] = trueAlgName[l1+1] = ':';
      strcpy(&trueAlgName[l1+2], s2);
      return trueAlgName;
  }

  /**
   *  We don't need, and don't want, to use the REGISTER_TM_FOR_XYZ macros,
   *  but we still need to make sure that there is an initTM<AdapTM> symbol:
   */
  template <> void initTM<AdapTM>() { }
}
