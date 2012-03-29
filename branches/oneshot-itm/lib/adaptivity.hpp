/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef ADAPTIVITY_HPP__
#define ADAPTIVITY_HPP__

/**
 * Include a file that is generated automatically and provides the names of
 * all our algorithms as an enum called TM_NAMES
 */
#include <tmnames.autobuild.h>
#include "tx.hpp"

namespace stm
{
  typedef uint32_t      (*tm_begin_t)(uint32_t);
  typedef void          (*tm_end_t)();
  typedef void*         (*tm_read_t)(void**) TM_FASTCALL;
  typedef void          (*tm_write_t)(void**, void*) TM_FASTCALL;
  typedef void*         (*tm_alloc_t)(size_t);
  typedef void          (*tm_free_t)(void*);
  typedef const char*   (*tm_get_alg_name_t)();
  typedef checkpoint_t* (*rollback_t)(TX*);

  /**
   * Use this function to register your TM algorithm implementation.  It
   * takes a bunch of function pointers and an identifier from the TM_NAMES
   * enum.  This should be called by the initTM<> method.
   */
  void registerTMAlg(int identifier,
                     tm_begin_t,
                     tm_end_t,
                     tm_read_t,
                     tm_write_t,
                     rollback_t,
                     tm_get_alg_name_t,
                     tm_alloc_t,
                     tm_free_t);

  /**
   *  We don't want to have to declare an init function for each of the STM
   *  algorithms that exist, because there are very many of them.  Instead,
   *  we have a templated init function in namespace stm, and we instantiate
   *  it once per algorithm, in the algorithm's .cpp, using the TM_NAMES
   *  enum.  Then we can just call the templated functions from this code,
   *  and the linker will find the corresponding instantiation.
   */
  template <int I>
  void initTM();

  /**
   *  Type for storing all the information we need to define an STM algorithm,
   *  and the array of all of the stored algorithms.
   */
  extern struct alg_t {
      int               identifier;
      tm_begin_t        tm_begin;
      tm_end_t          tm_end;
      tm_read_t         tm_read;
      tm_write_t        tm_write;
      rollback_t        rollback;
      tm_get_alg_name_t tm_getalgname;
      tm_alloc_t        tm_alloc;
      tm_free_t         tm_free;

      // [TODO]
      // bool (* irrevoc)(TxThread*);
      // void (* switcher) ();
      // bool privatization_safe;
  } tm_info[TM_NAMES_MAX];
}

/**
 * This hides the nastiness of registering algorithms with the adaptivity
 * mechanism
 */
#define REGISTER_TM_FOR_ADAPTIVITY(ALG)                                 \
    namespace stm {                                                     \
      template <> void initTM<ALG>() {                                  \
          registerTMAlg(ALG,                                            \
                        tm_begin, tm_end, tm_read, tm_write, rollback,  \
                        tm_getalgname, tm_alloc, tm_free);              \
        }                                                               \
    }

#endif // ADAPTIVITY_HPP__
