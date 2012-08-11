/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef PROFILING_HPP__
#define PROFILING_HPP__

/**
 *  This code handles the profiling mechanism.  It consists of three parts:
 *
 *  - The code for requesting that a bunch of profiles are collected
 *
 *  - The code for calling a policy after the profiles are collected, and using
 *    that result to change the algorithm
 *
 *  - The code that is called on every commit/abort by any transaction, to
 *    determine when a request should be initiated
 */

#include <cstdlib>
#include "../include/tlsapi.hpp"
#include "txthread.hpp"

namespace stm
{
  /**
   *  Data type for holding the dynamic transaction profiles that we collect.
   *  This is pretty sloppy for now, and the 'dump' command is really not
   *  going to be important once we get out of the debug phase.  We may also
   *  determine that we need more information than we currently have.
   */
  struct profile_t
  {
      int      read_ro;        // calls to read_ro
      int      read_rw_nonraw; // read_rw calls that are not raw
      int      read_rw_raw;    // read_rw calls that are raw
      int      write_nonwaw;   // write calls that are not waw
      int      write_waw;      // write calls that are waw
      int      pad;            // to put the 64-bit val on a 2-byte boundary
      uint64_t txn_time;       // txn time
      uint64_t timecounter;    // total time in transactions

      // to be clear: txn_time is either the average time for all
      // transactions, or the max time of any transaction.  timecounter is
      // the sum of all time in transactions.  timecounter only is useful for
      // profileapp, but it is very important if we want to compute nontx/tx
      // ratio when txn_time is a max value

      /**
       *  simple ctor to prevent compiler warnings
       */
      profile_t()
          : read_ro(0), read_rw_nonraw(0), read_rw_raw(0), write_nonwaw(0),
            write_waw(0), pad(0), txn_time(0), timecounter(0)
      {
      }

      /**
       *  Operator for copying profiles
       */
      profile_t& operator=(const profile_t* profile)
      {
          if (this != profile) {
              read_ro        = profile->read_ro;
              read_rw_nonraw = profile->read_rw_nonraw;
              read_rw_raw    = profile->read_rw_raw;
              write_nonwaw   = profile->write_nonwaw;
              write_waw      = profile->write_waw;
              txn_time       = profile->txn_time;
          }
          return *this;
      }

      /**
       *  Print a dynprof_t
       */
      void dump();

      /**
       *  Clear a dynprof_t
       */
      void clear()
      {
          read_ro = read_rw_nonraw = read_rw_raw = 0;
          write_nonwaw = write_waw = 0;
          txn_time = timecounter = 0;
      }

      /**
       *  If we have lots of profiles, compute their average value for each
       *  field
       */
      static void doavg(profile_t& dest, profile_t* list, int num)
      {
          // zero the important fields
          dest.clear();

          // accumulate sums into dest
          for (int i = 0; i < num; ++i) {
              dest.read_ro        += list[i].read_ro;
              dest.read_rw_nonraw += list[i].read_rw_nonraw;
              dest.read_rw_raw    += list[i].read_rw_raw;
              dest.write_nonwaw   += list[i].write_nonwaw;
              dest.write_waw      += list[i].write_waw;
              dest.txn_time       += list[i].txn_time;
          }

          // compute averages
          dest.read_ro        /= num;
          dest.read_rw_nonraw /= num;
          dest.read_rw_raw    /= num;
          dest.write_nonwaw   /= num;
          dest.write_waw      /= num;
          dest.txn_time       /= num;
      }
  };

  // [mfs] Is this padded well enough?
  extern profile_t*    app_profiles;                   // for ProfileApp*

  // ProfileTM can't function without these
  // [mfs] Are they padded well enough?
  extern profile_t*    profiles;          // a list of ProfileTM measurements
  extern uint32_t      profile_txns;      // how many txns per profile

  /**
   * After profiles are collected, select and install a new algorithm
   *
   * [mfs] Who calls this?  Why is it here?
   */
  void profile_oncomplete(TxThread* tx);


} // namespace stm

#endif // PROFILING_HPP__
