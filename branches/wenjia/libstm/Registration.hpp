/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef REGISTRATION_HPP__
#define REGISTRATION_HPP__

#include <stdint.h>
#include "../include/abstract_compiler.hpp"
#include "../include/macros.hpp"
#include <algnames-autogen.hpp> // defines the ALGS enum
#include "../include/tlsapi.hpp"

namespace stm
{
  struct TxThread;

  /*** Get an ENUM value from a string TM name */
  int32_t stm_name_map(const char*);

  /**
   *  To describe an STM algorithm, we provide a name, a set of function
   *  pointers, and some other information
   *
   *  [mfs] This should be part of "inst" in some way, since it relies on
   *        adaptivity being turned on...
   */
  struct alg_t
  {
      /*** the name of this policy */
      const char* name;

      /**
       * the begin, commit, read, and write methods a tx uses when it
       * starts
       */
      void  (* begin) (TX_LONE_PARAMETER);
      void  (*TM_FASTCALL commit)(TX_LONE_PARAMETER);
      void* (*TM_FASTCALL read)  (TX_FIRST_PARAMETER STM_READ_SIG(,));
      void  (*TM_FASTCALL write) (TX_FIRST_PARAMETER STM_WRITE_SIG(,,));

      /**
       * rolls the transaction back without unwinding, returns the scope (which
       * is set to null during rollback)
       */
      void (* rollback)(STM_ROLLBACK_SIG(,,));

      /*** the restart, retry, and irrevoc methods to use */
      bool  (* irrevoc)(TxThread*);

      /*** the code to run when switching to this alg */
      void  (* switcher) ();

      /**
       *  bool flag to indicate if an algorithm is privatization safe
       *
       *  NB: we should probably track levels of publication safety too, but
       *      we don't
       */
      bool privatization_safe;

      /*** simple ctor, because a NULL name is a bad thing */
      alg_t() : name("") { }
  };

  /**
   *  These describe all our STM algorithms and adaptivity policies
   */
  extern alg_t stms[ALG_MAX];

  /**
   *  We don't want to have to declare an init function for each of the STM
   *  algorithms that exist, because there are very many of them and they vary
   *  dynamically.  Instead, we have a templated init function in namespace stm,
   *  and we instantiate it once per algorithm, in the algorithm's .cpp, using
   *  the ALGS enum.  Then we can just call the templated functions from this
   *  code, and the linker will find the corresponding instantiation.
   */
  template <int I>
  void initTM();
}

#endif // REGISTRATION_HPP__
