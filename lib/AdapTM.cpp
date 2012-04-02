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
 *  Implements a simple, function-pointer-based version of adaptivity.
 *
 *  It is important, at least in Linux using ld.bfd, that the AdapTM.o object
 *  be listed first when linking libAdapTM.a. This is because we don't
 *  implement any symbols that _require_ AdapTM.o to be linked if the stm ABI
 *  symbols already have been resolved with weak symbols from other .os.
 */

#include <stdint.h>
#include <iostream>
#include <cstdlib>
#include "tmabi.hpp"                    // strong version overrides weak
#include "tmabi-fptr.hpp"               // function pointers
#include "tx.hpp"
#include "locks.hpp"
#include "metadata.hpp"
#include "adaptivity.hpp"

using namespace stm;

namespace {
  /**
   *  Stores the function pointers for the dynamically selectable algorithms,
   *  registered through registerTMAlg.
   */
  static struct {
      int                 identifier;
      tm_begin_t          tm_begin;
      tm_end_t            tm_end;
      tm_read_t           tm_read;
      tm_write_t          tm_write;
      tm_rollback_t       tm_rollback;
      tm_get_alg_name_t   tm_getalgname;
      tm_alloc_t          tm_alloc;
      tm_free_t           tm_free;
      tm_is_irrevocable_t tm_is_irrevocable;

      // [TODO]
      // void (* switcher) ();
      // bool privatization_safe;
  } tm_info[TM_NAMES_MAX];

  // [ld] why not a static inside of tm_getalgname? Are we avoiding the
  //      thread-safe initialization concern?
  static char* trueAlgName = NULL;

  /** Template Metaprogramming trick for initializing all STM algorithms. */
  template <int I>
  static void init_tm_info() {
      initTM<(TM_NAMES)I>();
      init_tm_info<I-1>();
  }

  template <>
  void init_tm_info<0>() {
      initTM<(TM_NAMES)0>();
  }

  static void init_tm_info() {
      init_tm_info<TM_NAMES_MAX - 1>();
  }

  /** Initialize all of the TM algorithms. */
  static void __attribute__((constructor)) tm_library_init() {
      // call of the initTM, to have them register themselves with tm_info
      init_tm_info();

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
              tm_rollback_ = tm_info[i].tm_rollback;
              tm_begin_ = tm_info[i].tm_begin;
              tm_end_ = tm_info[i].tm_end;
              tm_getalgname_ = tm_info[i].tm_getalgname;
              tm_alloc_ = tm_info[i].tm_alloc;
              tm_free_ = tm_info[i].tm_free;
              tm_read_ = tm_info[i].tm_read;
              tm_write_ = tm_info[i].tm_write;
              tm_is_irrevocable_ = tm_info[i].tm_is_irrevocable;
              found = true;
              break;
          }
      }
      printf("STM library configured using config == %s\n", cfg);
  }
}

namespace stm {
  tm_begin_t          tm_begin_;
  tm_end_t            tm_end_;
  tm_read_t           tm_read_;
  tm_write_t          tm_write_;
  tm_rollback_t       tm_rollback_;
  tm_get_alg_name_t   tm_getalgname_;
  tm_alloc_t          tm_alloc_;
  tm_free_t           tm_free_;
  tm_is_irrevocable_t tm_is_irrevocable_;

  /**
   *  A strong implementation of the registration algorithm.
   */
  void registerTMAlg(int identifier,
                     tm_begin_t tm_begin,
                     tm_end_t tm_end,
                     tm_read_t tm_read,
                     tm_write_t tm_write,
                     tm_rollback_t tm_rollback,
                     tm_get_alg_name_t tm_getalgname,
                     tm_alloc_t tm_alloc,
                     tm_free_t tm_free,
                     tm_is_irrevocable_t tm_is_irrevocable)
  {
      tm_info[identifier].identifier = identifier;
      tm_info[identifier].tm_begin = tm_begin;
      tm_info[identifier].tm_end = tm_end;
      tm_info[identifier].tm_read = tm_read;
      tm_info[identifier].tm_write = tm_write;
      tm_info[identifier].tm_rollback = tm_rollback;
      tm_info[identifier].tm_getalgname = tm_getalgname;
      tm_info[identifier].tm_alloc = tm_alloc;
      tm_info[identifier].tm_free = tm_free;
      tm_info[identifier].tm_is_irrevocable = tm_is_irrevocable;
  }

  // forward all calls to the function pointers
  uint32_t tm_begin(uint32_t flags, TX* tx) {
      return tm_begin_(flags, tx);
  }

  void tm_end() {
      tm_end_();
  }

  void* tm_alloc(size_t s) {
      return tm_alloc_(s);
  }

  void tm_free(void* p) {
      tm_free_(p);
  }

  void* tm_read(void** addr) {
      return tm_read_(addr);
  }

  void tm_write(void** addr, void* val) {
      tm_write_(addr, val);
  }

  void tm_rollback(TX* tx) {
      tm_rollback_(tx);
  }

  bool tm_is_irrevocable(TX* tx) {
      return tm_is_irrevocable_(tx);
  }

  const char* tm_getalgname() {
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
   *  but we still need to make sure that there is an initTM<AdapTM>
   *  symbol. This is because the name enum is manually generated.
   */
  template <> void initTM<AdapTM>() {
  }
}
