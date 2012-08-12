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
 *  ProfileApp Implementation
 *
 *    This is not a valid STM.  It exists only to provide a simple way to
 *    measure the overhead of collecting a profile, and to gather stats.  If
 *    you run a workload with ProfileApp instrumentation, you'll get no
 *    concurrency control, but the run time for each transaction will be
 *    roughly the same as what a ProfileTM transaction runtime would be.
 *
 *    We have two variants of this code, corresponding to when we count
 *    averages, and when we count maximum values.  It turns out that this is
 *    rather simple: we need only template the commit functions, so that we
 *    can aggregate statistics in two ways.
 */

#ifndef PROFILEAPP_HPP__
#define PROFILEAPP_HPP__

#include "../profiling.hpp"
#include "algs.hpp"
#include "../RedoRAWUtils.hpp"
#include "../Diagnostics.hpp"

/*** to distinguish between the two variants of this code */
#define __AVERAGE 1
#define __MAXIMUM 0

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace stm
{
  /**
   *  To support both average and max without too much overhead, we are going
   *  to template the implementation.  Then, we can specialize for two
   *  different int values, which will serve as a bool for either doing AVG (1)
   *  or MAX (0)
   */
  template <int COUNTMODE>
  TM_FASTCALL void* ProfileAppReadRO(TX_FIRST_PARAMETER STM_READ_SIG(,));
  template <int COUNTMODE>
  TM_FASTCALL void* ProfileAppReadRW(TX_FIRST_PARAMETER STM_READ_SIG(,));
  template <int COUNTMODE>
  TM_FASTCALL void ProfileAppWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  template <int COUNTMODE>
  TM_FASTCALL void ProfileAppWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(,,));
  template <int COUNTMODE>
  TM_FASTCALL void ProfileAppCommitRO(TX_LONE_PARAMETER);
  template <int COUNTMODE>
  TM_FASTCALL void ProfileAppCommitRW(TX_LONE_PARAMETER);

  /**
   *  Helper MACRO
   */
#define UPDATE_MAX(x,y) if((x) > (y)) (y) = (x)

  /**
   *  ProfileApp begin:
   *
   *    Start measuring tx runtime
   */
  template <int COUNTMODE>
  void ProfileAppBegin(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      tx->allocator.onTxBegin();
      profiles[0].txn_time = tick();
  }

  /**
   *  ProfileApp commit (read-only):
   *
   *    RO commit just involves updating statistics
   */
  template <int COUNTMODE>
  TM_FASTCALL
  void ProfileAppCommitRO(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // NB: statically optimized version of RW code for RO case
      unsigned long long runtime = tick() - profiles[0].txn_time;

      if (COUNTMODE == __MAXIMUM) {
          // update max values: only ro_reads and runtime change in RO transactions
          UPDATE_MAX(profiles[0].read_ro, app_profiles->read_ro);
          UPDATE_MAX(runtime,           app_profiles->txn_time);
      }
      else {
          // update totals: again, only ro_reads and runtime
          app_profiles->read_ro += profiles[0].read_ro;
          app_profiles->txn_time += runtime;
      }
      app_profiles->timecounter += runtime;

      // clear the profile, clean up the transaction
      profiles[0].read_ro = 0;
      OnROCommit(tx);
  }

  /**
   *  ProfileApp commit (writing context):
   *
   *    We need to replay writes, then update the statistics
   */
  template <int COUNTMODE>
  TM_FASTCALL
  void ProfileAppCommitRW(TX_LONE_PARAMETER)
  {
      TX_GET_TX_INTERNAL;
      // run the redo log
      tx->writes.writeback();
      // remember write set size before clearing it
      int x = tx->writes.size();
      tx->writes.reset();

      // compute the running time and write info
      unsigned long long runtime = tick() - profiles[0].txn_time;
      profiles[0].write_nonwaw = x;
      profiles[0].write_waw -= x;

      if (COUNTMODE == __MAXIMUM) {
          // update max values
          UPDATE_MAX(profiles[0].read_ro,        app_profiles->read_ro);
          UPDATE_MAX(profiles[0].read_rw_nonraw, app_profiles->read_rw_nonraw);
          UPDATE_MAX(profiles[0].read_rw_raw,    app_profiles->read_rw_raw);
          UPDATE_MAX(profiles[0].write_nonwaw,   app_profiles->write_nonwaw);
          UPDATE_MAX(profiles[0].write_waw,      app_profiles->write_waw);
          UPDATE_MAX(runtime,                    app_profiles->txn_time);
      }
      else {
          // update totals
          app_profiles->read_ro        += profiles[0].read_ro;
          app_profiles->read_rw_nonraw += profiles[0].read_rw_nonraw;
          app_profiles->read_rw_raw    += profiles[0].read_rw_raw;
          app_profiles->write_nonwaw   += profiles[0].write_nonwaw;
          app_profiles->write_waw      += profiles[0].write_waw;
          app_profiles->txn_time       += runtime;
      }
      app_profiles->timecounter += runtime;

      // clear the profile
      profiles[0].clear();

      // finish cleaning up
      OnRWCommit(tx);
      ResetToRO(tx, ProfileAppReadRO<COUNTMODE>, ProfileAppWriteRO<COUNTMODE>,
                ProfileAppCommitRO<COUNTMODE>);
  }

  /**
   *  ProfileApp read (read-only transaction)
   */
  template <int COUNTMODE>
  TM_FASTCALL
  void* ProfileAppReadRO(TX_FIRST_PARAMETER_ANON STM_READ_SIG(addr,))
  {
      // count the read
      ++profiles[0].read_ro;
      // read the actual value, direct from memory
      return *addr;
  }

  /**
   *  ProfileApp read (writing transaction)
   */
  template <int COUNTMODE>
  TM_FASTCALL
  void* ProfileAppReadRW(TX_FIRST_PARAMETER STM_READ_SIG( addr,mask))
  {
      TX_GET_TX_INTERNAL;
      // check the log for a RAW hazard, we expect to miss
      WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
      bool found = tx->writes.find(log);
      REDO_RAW_CHECK_PROFILEAPP(found, log, mask);

      // count this read, and get value from memory
      //
      // NB: There are other interesting stats when byte logging, should we
      //     record them?
      ++profiles[0].read_rw_nonraw;
      void* val = *addr;
      REDO_RAW_CLEANUP(val, found, log, mask);
      return val;
  }

  /**
   *  ProfileApp write (read-only context)
   */
  template <int COUNTMODE>
  TM_FASTCALL
  void ProfileAppWriteRO(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // do a buffered write
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      ++profiles[0].write_waw;
      stm::OnFirstWrite(tx, ProfileAppReadRW<COUNTMODE>,
                        ProfileAppWriteRW<COUNTMODE>,
                        ProfileAppCommitRW<COUNTMODE>);
  }

  /**
   *  ProfileApp write (writing context)
   */
  template <int COUNTMODE>
  TM_FASTCALL
  void ProfileAppWriteRW(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,mask))
  {
      TX_GET_TX_INTERNAL;
      // do a buffered write
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      ++profiles[0].write_waw;
  }

  /**
   *  ProfileApp unwinder:
   *
   *    Since this is a single-thread STM, it doesn't make sense to support
   *    abort, retry, or restart.
   */
  template <int COUNTMODE>
  void ProfileAppRollback(STM_ROLLBACK_SIG(,,))
  {
      stm::UNRECOVERABLE("ProfileApp should never incur an abort");
  }

  /**
   *  ProfileApp in-flight irrevocability:
   */
  template <int COUNTMODE>
  bool ProfileAppIrrevoc(TxThread*)
  {
      // NB: there is no reason why we can't support this, we just don't yet.
      stm::UNRECOVERABLE("ProfileApp does not support irrevocability");
      return false;
  }

  /**
   *  Switch to ProfileApp:
   *
   *    The only thing we need to do is make sure we have some dynprof_t's
   *    allocated for doing our logging
   */
  template <int COUNTMODE>
  void ProfileAppOnSwitchTo()
  {
      if (app_profiles != NULL)
          return;

      // allocate and configure the counters
      app_profiles = new profile_t();

      // set all to zero, since both counting and maxing begin with zero
      app_profiles->clear();
  }
}

#endif // PROFILEAPP_HPP__
