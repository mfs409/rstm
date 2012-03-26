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
  /**
   * Use this function to register your TM algorithm implementation.  It
   * takes a bunch of function pointers and an identifier from the TM_NAMES
   * enum.  This should be called by the initTM<> method.
   */
  void registerTMAlg(int identifier,
                     void (*tm_begin)(scope_t*),
                     void (*tm_end)(),
                     void* (* TM_FASTCALL tm_read)(void**),
                     void (* TM_FASTCALL tm_write)(void**, void*),
                     scope_t* (*rollback)(TX*),
                     const char* (*tm_getalgname)(),
                     void* (*tm_alloc)(size_t),
                     void (*tm_free)(void*));

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
   *  Type for storing all the information we need to define an STM algorithm
   */
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

  /**
   *  Collection of all known algorithms
   */
  extern alg_t tm_info[TM_NAMES_MAX];
}

/**
 * This hides the nastiness of registering algorithms with the adaptivity
 * mechanism
 */
#define REGISTER_TM_FOR_ADAPTIVITY(ALG, NS)                             \
    namespace stm                                                       \
    {                                                                   \
        template <> void initTM<ALG>()                                  \
        {                                                               \
            registerTMAlg(ALG, NS::tm_begin, NS::tm_end, NS::tm_read,   \
                          NS::tm_write, NS::rollback,                   \
                          NS::tm_getalgname, NS::tm_alloc,              \
                          NS::tm_free);                                 \
        }                                                               \
    }

#endif // ADAPTIVITY_HPP__
