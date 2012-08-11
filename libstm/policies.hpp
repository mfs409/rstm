/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef POLICIES_HPP__
#define POLICIES_HPP__

#ifdef STM_CC_SUN
#include <stdio.h>
#else
#include <cstdio>
#endif

#include <inttypes.h>
#include "../include/abstract_compiler.hpp"
#include "MiniVector.hpp"
#include "Constants.hpp"
#include "profiling.hpp"

namespace stm
{
  /**
   *  These define the different adaptivity policies.  A policy is a name, the
   *  starting mode, and some information about how/when to adapt.
   */
  struct pol_t
  {
      /*** the name of this policy */
      const char* name;

      /*** name of the mode that we start in */
      int   startmode;

      /*** thresholds for adapting due to aborts and waiting */
      int   abortThresh;
      int   waitThresh;
      uint32_t roThresh;

      /*** does the policy use profiles? */
      bool  isDynamic;

      /*** does the policy require a qtable? */
      bool isCBR;

      /*** does the policy have commit-based reprofiling? */
      bool isCommitProfile;

      /*** the decision policy function pointer */
      uint32_t (*TM_FASTCALL decider) ();

      /*** simple ctor, because a NULL name is a bad thing */
      pol_t() : name(""), roThresh(INT_MAX) { }
  };

  /**
   *  This describes the state of the selected policy.  This should be a
   *  singleton, but we don't bother.  There will be one of these, which we
   *  can use to tell what the current policy is that libstm is using.
   */
  struct behavior_t
  {
      // name of current policy
      uint32_t POL_ID;

      // name of current algorithm
      volatile uint32_t ALG_ID;

      // name of alg before the last profile was collected
      uint32_t PREPROFILE_ALG;

      // did we make a decision due to aborting?
      bool abort_switch;

      // was this decision based on an explicit request by the current STM
      // implementation?
      bool requested_switch;

      // so we can backoff on our thresholds when we have repeat
      // algorithim selections
      int abortThresh;
      int waitThresh;
  };


  /*** Used in txthread to initialize the policy subsystem */
  void pol_init();

  /**
   *  Just like stm_name_map, we sometimes need to turn a policy name into its
   *  corresponding enum value
   */
  int pol_name_map(const char* phasename);

  /**
   *  The POLS enum lists every adaptive policy we have
   */
  enum POLS {
      // this is a "no adaptivity" policy
      Single,
      // Testing policy, to make sure profiles are working
      PROFILE_NOCHANGE,
      // the state-machine policies
      E, ER, R, X,
      // test policies
      MFS_NOL, MFS_TOL,
      // CBR without dynamic profiling
      CBR_RO,
      // CBR with dynamic profiling
      CBR_Read, CBR_Write, CBR_Time, CBR_RW,
      CBR_R_RO, CBR_R_Time, CBR_W_RO, CBR_W_Time,
      CBR_Time_RO, CBR_R_W_RO, CBR_R_W_Time, CBR_R_Time_RO,
      CBR_W_Time_RO, CBR_R_W_Time_RO, CBR_TxnRatio, CBR_TxnRatio_R,
      CBR_TxnRatio_W, CBR_TxnRatio_RO, CBR_TxnRatio_Time, CBR_TxnRatio_RW,
      CBR_TxnRatio_R_RO, CBR_TxnRatio_R_Time, CBR_TxnRatio_W_RO,
      CBR_TxnRatio_W_Time, CBR_TxnRatio_RO_Time, CBR_TxnRatio_RW_RO,
      CBR_TxnRatio_RW_Time, CBR_TxnRatio_R_RO_Time, CBR_TxnRatio_W_RO_Time,
      CBR_TxnRatio_RW_RO_Time,
      // max value... this always goes last
      POL_MAX
  };

  /**
   *  These globals are used by our adaptivity policies
   */
  extern pol_t                 pols[POL_MAX];       // describe all policies
  extern behavior_t            curr_policy;         // the current STM alg

  /*** used in the policies impementations to register policies */
  void init_adapt_pol(uint32_t PolicyID,   int32_t startmode,
                      int32_t abortThresh, int32_t waitThresh,
                      bool isDynamic,      bool isCBR,
                      bool isCommitProfile,  uint32_t (*decider)() TM_FASTCALL,
                      const char* name);

} // namespace stm

#endif // POLICIES_HPP__
