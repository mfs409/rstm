/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef CM_HPP__
#define CM_HPP__

#include <limits.h>
#include "../include/abstract_cpu.hpp"
#include "txthread.hpp"
#include "SpinLocks.hpp"
#include "inst.hpp"

  /**
   *  Define the states for a transaction
   *
   * [mfs] Probably not the right place for these..
   */
#define TX_ACTIVE     0u
#define TX_ABORTED    1u
#define TX_COMMITTED  2u

/**
 *  Define the CM policies that can be plugged into our framework.  For the
 *  time being, these only make sense in the context of attacker-wins conflict
 *  management
 */
namespace stm
{
  extern pad_word_t    fcm_timestamp;                  // for FCM
  extern pad_word_t    epochs[MAX_THREADS];            // for coarse-grained CM

  /**
   *  A simple implementation of randomized exponential backoff.
   *
   *  NB: This uses getElapsedTime, which is slow compared to a granularity
   *      of 64 nops.  However, we can't switch to tick(), because sometimes
   *      two successive tick() calls return the same value and tickp isn't
   *      universal.
   */
  void exp_backoff(TxThread* tx);

  /**
   *  Backoff CM policy: On abort, perform randomized exponential backoff
   */
  struct BackoffCM
  {
      static void onAbort(TxThread* tx)
      {
          // randomized exponential backoff
          exp_backoff(tx);
      }
      static void onBegin(TxThread*)  { }
      static void onCommit(TxThread*) { }
      static bool mayKill(TxThread*, uint32_t) { return true; }
  };

  /**
   *  HyperAggressive CM policy: don't do backoff, just try to win all the time
   */
  struct HyperAggressiveCM
  {
      static void onAbort(TxThread*) { }
      static void onBegin(TxThread*)  { }
      static void onCommit(TxThread*) { }
      static bool mayKill(TxThread*, uint32_t) { return true; }
  };

  /**
   *  Fine-grained CM: we get a timestamp, and use it to decide when to abort
   *  the other thread.  This is not exactly an attacker-wins policy anymore
   *
   *  This is based on a concept from Bobba et al. ISCA 07
   */
  struct FCM
  {
      static void onAbort(TxThread*) { }
      static void onCommit(TxThread*) { }

      /**
       *  On begin, we must get a timestamp.  For now, we use a global
       *  counter, which is a bottleneck but ensures uniqueness.
       */
      static void onBegin(TxThread* tx)
      {
          // acquire timestamp when transaction begins
          epochs[tx->id-1].val = faiptr(&fcm_timestamp.val);
          // could use (INT32_MAX & tick())
      }

      /**
       *  Permission to kill the other is granted when this transaction's
       *  timestamp is less than the other transaction's timestamp
       */
      static bool mayKill(TxThread* tx, uint32_t other)
      {
          return (threads[tx->id-1]->alive == TX_ACTIVE)
              && (epochs[tx->id-1].val < epochs[other].val);
      }
  };

  /**
   *  StrongHourglass CM: a concerned transaction serializes all execution.
   *  The aborted transaction who wishes to enter the hourglass waits until he
   *  can do so
   */
  struct StrongHourglassCM
  {
      static const uint32_t ABORT_THRESHOLD = 2;

      /**
       *  On begin, block if there is a distinguished transaction
       */
      static void onBegin(TxThread* tx)
      {
          if (!tx->strong_HG)
              while (fcm_timestamp.val)
                  if (tmbegin == begin_blocker)
                      tmabort();
      }

      /**
       *  On abort, get a timestamp if I exceed some threshold
       */
      static void onAbort(TxThread* tx)
      {
          // if I'm already in the hourglass, just return
          if (tx->strong_HG) {
              tx->abort_hist.onHGAbort();
              return;
          }

          // acquire a timestamp if consecutive aborts exceed a threshold
          if (tx->consec_aborts > ABORT_THRESHOLD) {
              while (true) {
                  if (bcasptr(&fcm_timestamp.val, 0ul, 1ul)) {
                      tx->strong_HG = true;
                      return;
                  }
                  while (fcm_timestamp.val) { }
              }
          }
          // NB: It would be good to explore what happens if I have a
          //     strong_HG already?  Can we count how many times I abort with
          //     strong_HG?
      }

      /**
       *  On commit, release my timestamp
       */
      static void onCommit(TxThread* tx)
      {
          if (tx->strong_HG) {
              fcm_timestamp.val = 0;
              tx->strong_HG = false;
              tx->abort_hist.onHGCommit();
          }
      }

      /**
       *  During the transaction, always abort conflicting transactions
       */
      static bool mayKill(TxThread*, uint32_t) { return true; }
  };

  /**
   *  Hourglass CM: a concerned transaction serializes all execution
   */
  struct HourglassCM
  {
      static const uint32_t ABORT_THRESHOLD = 2;

      /**
       *  On begin, block if there is a distinguished transaction
       */
      static void onBegin(TxThread* tx)
      {
          if (!tx->strong_HG)
              while (fcm_timestamp.val)
                  if (tmbegin == begin_blocker)
                      tmabort();
      }

      /**
       *  On abort, get a timestamp if I exceed some threshold
       */
      static void onAbort(TxThread* tx)
      {
          // if I'm already in the hourglass, just return
          if (tx->strong_HG) {
              tx->abort_hist.onHGAbort();
              return;
          }

          // acquire a timestamp if consecutive aborts exceed a threshold
          if (tx->consec_aborts > ABORT_THRESHOLD)
              if (bcasptr(&fcm_timestamp.val, 0ul, 1ul))
                  tx->strong_HG = true;
          // NB: as before, some counting opportunities here
      }

      /**
       *  On commit, release my timestamp
       */
      static void onCommit(TxThread* tx)
      {
          if (tx->strong_HG) {
              fcm_timestamp.val = 0;
              tx->strong_HG = false;
              tx->abort_hist.onHGCommit();
          }
      }

      /**
       *  During the transaction, always abort conflicting transactions
       */
      static bool mayKill(TxThread*, uint32_t) { return true; }
  };

  /**
   *  Hourglass+Backoff CM: a concerned transaction serializes all execution
   */
  struct HourglassBackoffCM
  {
      static const uint32_t ABORT_THRESHOLD = 2;

      /**
       *  On begin, block if there is a distinguished transaction
       */
      static void onBegin(TxThread* tx)
      {
          if (!tx->strong_HG)
              while (fcm_timestamp.val)
                  if (tmbegin == begin_blocker)
                      tmabort();
      }

      /**
       *  On abort, get a timestamp if I exceed some threshold
       */
      static void onAbort(TxThread* tx)
      {
          // if I'm already in the hourglass, just return
          if (tx->strong_HG) {
              tx->abort_hist.onHGAbort();
              return;
          }

          // acquire a timestamp if consecutive aborts exceed a threshold
          if (tx->consec_aborts > ABORT_THRESHOLD) {
              if (bcasptr(&fcm_timestamp.val, 0ul, 1ul))
                  tx->strong_HG = true;
          }
          else {
              // randomized exponential backoff
              exp_backoff(tx);
          }
      }

      /**
       *  On commit, release my timestamp
       */
      static void onCommit(TxThread* tx)
      {
          if (tx->strong_HG) {
              fcm_timestamp.val = 0;
              tx->strong_HG = false;
              tx->abort_hist.onHGCommit();
          }
      }

      /**
       *  During the transaction, always abort conflicting transactions
       */
      static bool mayKill(TxThread*, uint32_t) { return true; }
  };

}

#endif // CM_HPP__
