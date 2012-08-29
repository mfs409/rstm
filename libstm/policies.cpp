/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "policies.hpp"
#include "initializers.hpp"    // init_pol_*
//#include "algs.hpp"
#include "cbr.hpp"

namespace stm
{
  /**
   *  Store the STM algorithms and adaptivity policies, so we can
   *  select one at will.  The selected one is in curr_policy.
   */
  pol_t      pols[POL_MAX];
  behavior_t curr_policy = {0, 0, 0, false, false, 0, 0};

  /*** the qtable for CBR policies */
  MiniVector<qtable_t>* qtbl[MAX_THREADS+1]  = {NULL};

  /*** Use the policies array to map a string name to a policy ID */
  int pol_name_map(const char* phasename)
  {
      for (int i = 0; i < POL_MAX; ++i)
          if (0 == strcmp(phasename, pols[i].name))
              return i;
      return -1;
  }

  /*** SUPPORT CODE FOR INITIALIZING ALL ADAPTIVITY POLICIES */

  /**
   *  This helper function lets us easily configure our STM adaptivity
   *  policies.  The idea is that an adaptive policy can get most of its
   *  configuration from the info in its starting state, and the rest of the
   *  information is easy to provide
   */
  void init_adapt_pol(uint32_t PolicyID,   int32_t startmode,
                      int32_t abortThresh, int32_t waitThresh,
                      bool isDynamic,      bool isCBR,
                      bool isCommitProfile,  uint32_t TM_FASTCALL (*decider)(),
                      const char* name)
  {
      pols[PolicyID].startmode       = startmode;
      pols[PolicyID].abortThresh     = abortThresh;
      pols[PolicyID].waitThresh      = waitThresh;
      pols[PolicyID].isDynamic       = isDynamic;
      pols[PolicyID].isCBR           = isCBR;
      pols[PolicyID].isCommitProfile = isCommitProfile;
      pols[PolicyID].decider         = decider;
      pols[PolicyID].name            = name;
  }


  /*** for initializing the adaptivity policies */
  void pol_init()
  {
      // call all initialization functions
      init_pol_static();
      init_pol_cbr();

      // load in the qtable here
      char* qstr = getenv("STM_QTABLE");
      if (qstr != NULL)
          load_qtable(qstr);
  }

  /**
   *  Print a dynprof_t
   *
   *  [mfs] Move to a cpp... we don't want to pull in cstdio!
   */
  void profile_t::dump()
  {
      printf("Profile: read_ro=%d, read_rw_nonraw=%d, read_rw_raw=%d, "
             "write_nonwaw=%d, write_waw=%d, txn_time=%llu\n",
             read_ro, read_rw_nonraw, read_rw_raw, write_nonwaw,
             write_waw, (unsigned long long)txn_time);
  }

} // namespace stm
