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

namespace {
  // [ld] why not a static inside of tm_getalgname? Are we avoiding the
  //      thread-safe initialization concern?
  static char* trueAlgName = NULL;

  // local function pointers that aren't exposed through the tmabi-fptr.h
  static tm_calloc_t tm_calloc_;
  static tm_become_irrevocable_t tm_become_irrevocable_;
  static tm_is_irrevocable_t tm_is_irrevocable_;

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

      bool isName(const char* const name) const {
          return (strcmp(name, tm_getalgname()) == 0);
      }

      void install() const {
          tm_rollback_ = tm_rollback;
          tm_begin_ = tm_begin;
          tm_end_ = tm_end;
          tm_getalgname_ = tm_getalgname;
          tm_alloc_ = tm_alloc;
          tm_calloc_ = tm_calloc;
          tm_free_ = tm_free;
          tm_read_ = tm_read;
          tm_write_ = tm_write;
          tm_become_irrevocable_ = tm_become_irrevocable;
          tm_is_irrevocable_ = tm_is_irrevocable;
      }

      // [TODO]
      // void (* switcher) ();
      // bool privatization_safe;
  } tm_info[TM_NAMES_MAX];

  static void print_tm_names(FILE* f) {
      for (int i = 0; i < TM_NAMES_MAX; ++i) {
          if (tm_info[i].tm_getalgname)
              fprintf(f, "\t%s\n", tm_info[i].tm_getalgname());
      }
  }

  /**
   *  Template metaprogramming cleverness for initialization. Initialize
   *  algorithm I and check to see if it's the algorithm that we've
   */
  template <int I>
  static void init(const char* cfg) {
      // initialize this algorithm
      initTM<I>();

      // if we're still looking for the right algorithm, and this one is it,
      // then install its pointers and set cfg to NULL to indicate that we
      // found what we needed (tm_info[I] has been initialized).
      if (cfg && tm_info[I].isName(cfg)) {
          tm_info[I].install();
          cfg = NULL;
      }

      // initialize the next algorthim
      init<I-1>(cfg);
  }

  /** AdapTM doesn't do anything special, just forward to the next init. */
  template <>
  void init<AdapTM>(const char* cfg) {
      init<AdapTM-1>(cfg);
  }

  /**
   *  All of the algorithms have been initialized at this point, do some error
   *  checking and terminate init recursion.
   */
  template <>
  void init<-1>(const char* const cfg) {
      // if cfg was specified, but we didn't find it, then we were configured
      // without a valid STM_CONFIG
      if (cfg) {
          fprintf(stderr,
                  "-------------------------------------------\n"
                  "Failed to initialize RSTM with config: %s\n"
                  "-------------------------------------------\n"
                  "Valid options are:\n", cfg);
          print_tm_names(stderr);
          exit(1);
      }

      // if cfg is NULL, but tm_getalgname_ wasn't set, that indicates that
      // getenv("STM_CONFIG") was null---we no longer have a default, so we
      // need to complain.
      if (!tm_getalgname_) {
          fprintf(stderr,
                  "-------------------------------------------------------\n"
                  "You must set the STM_CONFIG environment variable with a\n"
                  "valid algorithm name to use RSTM's adaptive interface.\n"
                  "-------------------------------------------------------\n"
                  "Valid options are:\n");
          print_tm_names(stderr);
          exit(1);
      }

      printf("STM library configured using config == %s\n", tm_getalgname_());
  }

  /**
   *  Initialize all of the TM algorithms from a static constructor, using the
   *  STM_CONFIG environment variable. If STM_CONFIG isn't set, no algorithm
   *  will be installed, and we'll generate an error once all of the tm_info
   *  slots have been filled.
   */
  static void __attribute__((constructor)) initialize_adaptivity() {
      init<TM_NAMES_MAX - 1>(getenv("STM_CONFIG"));
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
