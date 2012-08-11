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
 *  This file declares global metadata that is used by all STM algorithms,
 *  along with some accessor functions
 */

#ifndef ALGS_HPP__
#define ALGS_HPP__

#include "txthread.hpp"
#include "profiling.hpp" // Trigger::
#include "inst.hpp"
#include <algnames-autogen.hpp> // defines the ALGS enum
#include "../include/abstract_timing.hpp"

// [mfs] this isn't the right place for these defines, but they help to
//       reduce code size and the prominence of this placement will hopefully
//       lead to it being cleaned up properly soon...
#define COHORTS_COMMITTED 0
#define COHORTS_STARTED   1
#define COHORTS_CPENDING  2
#define COHORTS_NOTDONE   3
#define COHORTS_DONE      4
const uintptr_t VALIDATION_FAILED = 1;

namespace stm
{
  /**
   *  These constants are used throughout the STM implementations
   */
  const uint32_t RING_ELEMENTS = 1024;     // number of ring elements
  const uint32_t KARMA_FACTOR  = 16;       // aborts b4 incr karma
  const uint32_t EPOCH_MAX     = INT_MAX;  // default epoch
  const uint32_t ACTIVE        = 0;        // transaction status
  const uint32_t ABORTED       = 1;        // transaction status

  /**
   *  These global fields are used for concurrency control and conflict
   *  detection in our STM systems
   */
  extern pad_word_t    timestamp;
  extern pad_word_t    last_init;                      // last logical commit
  extern pad_word_t    last_complete;                  // last physical commit
  extern filter_t ring_wf[RING_ELEMENTS] TM_ALIGN(16); // ring of Bloom filters
  extern pad_word_t    prioTxCount;                    // # priority txns
  extern pad_word_t    timestamp_max;                  // max value of timestamp
  // [mfs] Is this padded well enough?
  extern mcs_qnode_t*  mcslock;                        // for MCS
  extern pad_word_t    epochs[MAX_THREADS];            // for coarse-grained CM
  // [mfs] Is this padded well enough?
  extern ticket_lock_t ticketlock;                     // for ticket lock STM
  extern pad_word_t    greedy_ts;                      // for swiss cm
  extern pad_word_t    fcm_timestamp;                  // for FCM
  // [mfs] Is this padded well enough?
  extern dynprof_t*    app_profiles;                   // for ProfileApp*

  // ProfileTM can't function without these
  // [mfs] Are they padded well enough?
  extern dynprof_t*    profiles;          // a list of ProfileTM measurements
  extern uint32_t      profile_txns;      // how many txns per profile

  // Global variables for Cohorts
  // [mfs] Do we want padding on this or not?
  extern volatile uint32_t locks[9];  // a big lock at locks[0], and
                                      // small locks from locks[1] to locks[8]
  extern  pad_word_t started;         // number of tx started
  extern  pad_word_t cpending;        // number of tx waiting to commit
  extern  pad_word_t committed;       // number of tx committed
  // [mfs] Do these need padding?  What algs use them?
  extern volatile int32_t last_order; // order of last tx in a cohort + 1
  extern volatile uint32_t gatekeeper;// indicating whether tx can start
  extern filter_t* global_filter;     // global filter
  extern filter_t* temp_filter;       // temp filter

  // Global variables for Fastlane
  extern pad_word_t helper;

  // Global variables for PTM
  extern pad_word_t global_version;
  extern pad_word_t writer_lock;

  /**
   *  To describe an STM algorithm, we provide a name, a set of function
   *  pointers, and some other information
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
   *  We don't want to have to declare an init function for each of the STM
   *  algorithms that exist, because there are very many of them and they vary
   *  dynamically.  Instead, we have a templated init function in namespace stm,
   *  and we instantiate it once per algorithm, in the algorithm's .cpp, using
   *  the ALGS enum.  Then we can just call the templated functions from this
   *  code, and the linker will find the corresponding instantiation.
   */
  template <int I>
  void initTM();

  /**
   *  These describe all our STM algorithms and adaptivity policies
   */
  extern alg_t stms[ALG_MAX];

  /*** Get an ENUM value from a string TM name */
  int32_t stm_name_map(const char*);

  /**
   *  A simple implementation of randomized exponential backoff.
   *
   *  NB: This uses getElapsedTime, which is slow compared to a granularity
   *      of 64 nops.  However, we can't switch to tick(), because sometimes
   *      two successive tick() calls return the same value and tickp isn't
   *      universal.
   */
  void exp_backoff(TxThread* tx);

  inline void OnRWCommit(TxThread* tx)
  {
      tx->allocator.onTxCommit();
      tx->abort_hist.onCommit(tx->consec_aborts);
      tx->consec_aborts = 0;
      tx->consec_ro = 0;
      ++tx->num_commits;
      Trigger::onCommitSTM(tx);
  }

  inline void OnROCommit(TxThread* tx)
  {
      tx->allocator.onTxCommit();
      tx->abort_hist.onCommit(tx->consec_aborts);
      tx->consec_aborts = 0;
      ++tx->consec_ro;
      ++tx->num_ro;
      Trigger::onCommitSTM(tx);
  }

  inline void OnCGLCommit(TxThread* tx)
  {
      tx->allocator.onTxCommitImmediate();
      tx->consec_ro = 0;
      ++tx->num_commits;
      Trigger::onCommitLock(tx);
  }

  inline void PostRollback(TxThread* tx)
  {
      tx->allocator.onTxAbort();
      tx->nesting_depth = 0;
      Trigger::onAbort(tx);
      tx->in_tx = false;
  }

  inline void PreRollback(TxThread* tx)
  {
      ++tx->num_aborts;
      ++tx->consec_aborts;
  }

  inline void OnROCGLCommit(TxThread* tx)
  {
      tx->allocator.onTxCommit();
      ++tx->consec_ro;
      ++tx->num_ro;
      Trigger::onCommitLock(tx);
  }

  /**
   *  Custom PostRollback code for ProfileTM.  If the last transaction in the
   *  profile set aborts, it will call profile_oncomplete before calling this.
   *  That means that it will adapt /out of/ ProfileTM, which in turn means
   *  that we cannot reset the pointers on abort.
   */
  inline void PostRollbackNoTrigger(TxThread* tx)
  {
      tx->allocator.onTxAbort();
      tx->nesting_depth = 0;
      tx->in_tx = false;
  }

} // namespace stm

#endif // ALGS_HPP__
