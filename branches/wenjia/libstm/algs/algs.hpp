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

#include <stdint.h>
#include <limits.h>
#include "../txthread.hpp"
#include "../Triggers.hpp"
#include "../Registration.hpp"
#include "../inst.hpp"
#include "../RedoRAWUtils.hpp"

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
  // [mfs] Is this padded well enough?
  extern ticket_lock_t ticketlock;                     // for ticket lock STM
  extern pad_word_t    greedy_ts;                      // for swiss cm

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
