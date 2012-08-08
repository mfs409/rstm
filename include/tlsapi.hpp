/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.ISM for licensing information
 */

#ifndef TLSAPI_HPP__
#define TLSAPI_HPP__

#ifdef STM_API_TLSPARAM
#include "ThreadLocal.hpp"

  namespace stm { class TxThread; }
  /*** GLOBAL VARIABLES RELATED TO THREAD MANAGEMENT */
  namespace stm {extern THREAD_LOCAL_DECL_TYPE(TxThread*) Self; }

#  define TX_LONE_PARAMETER  stm::TxThread* tx
#  define TX_FIRST_PARAMETER stm::TxThread* tx,
#  define TX_LONE_ARG tx
#  define TX_FIRST_ARG tx,
#  define TX_GET_TX stm::TxThread* tx = (stm::TxThread*)stm::Self
#  define TX_FIRST_PARAMETER_ANON stm::TxThread*,
#  define TX_GET_TX_INTERNAL
#else
#  define TX_LONE_PARAMETER
#  define TX_FIRST_PARAMETER
#  define TX_LONE_ARG
#  define TX_FIRST_ARG
#  define TX_GET_TX
#  define TX_FIRST_PARAMETER_ANON
#  define TX_GET_TX_INTERNAL TxThread* tx = stm::Self
#endif

#endif // TLSAPI_HPP__
