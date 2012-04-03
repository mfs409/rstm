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
#include "libitm.h"                     // _ITM_commitTransaction

using namespace stm;

/**
 *  We don't need, and don't want, to use the REGISTER_TM_FOR_XYZ macros, but
 *  we still need to make sure that there is an initTM<AdapTM> symbol. This is
 *  because the name enum is manually generated.
 */
namespace stm {
  template <> void initTM<AdapTM>() {
  }
}


namespace {
  /**
   *  Stores the function pointers for the dynamically selectable algorithms,
   *  registered through registerTMAlg.
   */
  static struct {
      tm_begin_t              tm_begin;
      tm_end_t                tm_end;
      tm_read_t               tm_read;
      tm_write_t              tm_write;
      tm_rollback_t           tm_rollback;
      tm_get_alg_name_t       tm_getalgname;
      tm_alloc_t              tm_alloc;
      tm_calloc_t             tm_calloc;
      tm_free_t               tm_free;
      tm_is_irrevocable_t     tm_is_irrevocable;
      tm_become_irrevocable_t tm_become_irrevocable;

      // [TODO]
      // void (* switcher) ();
      // bool privatization_safe;
  } tm_info[TM_NAMES_MAX];

  // [ld] why not a static inside of tm_getalgname? Are we avoiding the
  //      thread-safe initialization concern?
  static char* trueAlgName = NULL;

  // local function pointers that aren't exposed through the tmabi-fptr.h
  static tm_calloc_t tm_calloc_;
  static tm_become_irrevocable_t tm_become_irrevocable_;
  static tm_is_irrevocable_t tm_is_irrevocable_;

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
              tm_calloc_ = tm_info[i].tm_calloc;
              tm_free_ = tm_info[i].tm_free;
              tm_read_ = tm_info[i].tm_read;
              tm_write_ = tm_info[i].tm_write;
              tm_become_irrevocable_ = tm_info[i].tm_become_irrevocable;
              tm_is_irrevocable_ = tm_info[i].tm_is_irrevocable;
              found = true;
              break;
          }
      }
      printf("STM library configured using config == %s\n", cfg);
  }
}

tm_begin_t          stm::tm_begin_;
tm_end_t            stm::tm_end_;
tm_read_t           stm::tm_read_;
tm_write_t          stm::tm_write_;
tm_rollback_t       stm::tm_rollback_;
tm_get_alg_name_t   stm::tm_getalgname_;
tm_alloc_t          stm::tm_alloc_;
tm_free_t           stm::tm_free_;

/**
 *  A strong implementation of the registration algorithm.
 */
void stm::registerTMAlg(int tmid,
                        tm_begin_t tm_begin,
                        tm_end_t tm_end,
                        tm_read_t tm_read,
                        tm_write_t tm_write,
                        tm_rollback_t tm_rollback,
                        tm_get_alg_name_t tm_getalgname,
                        tm_alloc_t tm_alloc,
                        tm_calloc_t tm_calloc,
                        tm_free_t tm_free,
                        tm_is_irrevocable_t tm_is_irrevocable,
                        tm_become_irrevocable_t tm_become_irrevocable)
{
    tm_info[tmid].tm_begin = tm_begin;
    tm_info[tmid].tm_end = tm_end;
    tm_info[tmid].tm_read = tm_read;
    tm_info[tmid].tm_write = tm_write;
    tm_info[tmid].tm_rollback = tm_rollback;
    tm_info[tmid].tm_getalgname = tm_getalgname;
    tm_info[tmid].tm_alloc = tm_alloc;
    tm_info[tmid].tm_calloc = tm_calloc;
    tm_info[tmid].tm_free = tm_free;
    tm_info[tmid].tm_is_irrevocable = tm_is_irrevocable;
    tm_info[tmid].tm_become_irrevocable = tm_become_irrevocable;
}

uint32_t stm::tm_begin(uint32_t flags, TX* tx) {
    return tm_begin_(flags, tx);
}

void* stm::tm_read(void** addr) {
    return tm_read_(addr);
}

void stm::tm_write(void** addr, void* val) {
    tm_write_(addr, val);
}

void stm::tm_rollback(TX* tx) {
    tm_rollback_(tx);
}

bool stm::tm_is_irrevocable(TX* tx) {
    return tm_is_irrevocable_(tx);
}

const char* stm::tm_getalgname() {
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

void _ITM_commitTransaction() {
    tm_end_();
}

void _ITM_changeTransactionMode(_ITM_transactionState s) {
    tm_become_irrevocable_(s);
}

void* _ITM_malloc(size_t s) {
    return tm_alloc_(s);
}

void* _ITM_calloc(size_t n, size_t s) {
    return tm_calloc_(n, s);
}

void _ITM_free(void* p) {
    return tm_free_(p);
}
