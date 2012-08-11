/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef CBR_HPP__
#define CBR_HPP__

namespace stm
{
  /**
   *  This is for storing our CBR-style qtable
   *
   *    The qtable tells us for a particular workload characteristic, what
   *    algorithm did best at each thread count.
   */
  struct qtable_t
  {
      /**
       *  Selection Fields
       *
       *    NB: These fields are for choosing the output: For a given behavior,
       *        choose the algorithm that maximizes throughput.
       */

      /*** The name of the STM algorithm that produced this result */
      int alg_name;

      /**
       *  Transaction Behavior Summary
       *
       *    NB: The profile holds a characterization of the transactions of the
       *        workload, with regard to reads and writes, and the time spent
       *        on a transaction.  Depending on which variant of ProfileApp was
       *        used to create this profile, it will either hold average values,
       *        or max values.
       *
       *    NB: We assume that a summary of transactions in the single-thread
       *        execution is appropriate for the behavior of transactions in a
       *        multithreaded execution.
       */
      profile_t p;

      /**
       *  Workload Behavior Summary
       */

      /**
       *  The ratio of transactional work to nontransactional work
       */
      int txn_ratio;

      /**
       *  The percentage of transactions that are Read-Only
       */
      int pct_ro;

      /*** The thread count at which this result was measured */
      int thr;

      /*** really simple ctor */
      qtable_t() : pct_ro(0) { }
  };

  extern MiniVector<qtable_t>* qtbl[MAX_THREADS+1]; // hold the CBR data

  void load_qtable(char*& qstr);

} // namespace stm

#endif // CBR_HPP__
