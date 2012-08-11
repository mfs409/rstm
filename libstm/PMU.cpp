/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "PMU.hpp"

namespace stm
{
  /**
   *  For now, we're going to put all the PMU support into this file, and not
   *  worry about inlining overhead.  Note that we still need the ifdef guard,
   *  because we can't implement these if PAPI isn't available.
   */
#ifdef STM_USE_PMU

  /**
   *  Very Bad Programming Warning: this is a very dirty way to implement an
   *  array of tuples, where each entry is a PAPI code (int), the PAPI code as
   *  a string, and the PAPI code's description.
   */
  int keys[1024] = {0};
  const char* keystrs[1024] = {NULL};
  const char* descs[1024] = {NULL};
  int num_keys = 0;

  /**
   *  Helper method for adding to the tuple array
   */
  void addevent(int key, const char* keystr, const char* desc)
  {
      keys[num_keys] = key;
      keystrs[num_keys] = keystr;
      descs[num_keys] = desc;
      ++num_keys;
  }

  /**
   *  Insert into tuple array all PAPI options
   */
  void cfg()
  {
      addevent(PAPI_BR_CN, "PAPI_BR_CN", "Conditional branch instructions executed");
      addevent(PAPI_BR_INS, "PAPI_BR_INS", "Total branch instructions executed");
      addevent(PAPI_BR_MSP, "PAPI_BR_MSP", "Conditional branch instructions mispred");
      addevent(PAPI_BR_NTK, "PAPI_BR_NTK", "Conditional branch instructions not taken");
      addevent(PAPI_BR_PRC, "PAPI_BR_PRC", "Conditional branch instructions corr. pred");
      addevent(PAPI_BR_TKN, "PAPI_BR_TKN", "Conditional branch instructions taken");
      addevent(PAPI_BR_UCN, "PAPI_BR_UCN", "Unconditional branch instructions executed");
      addevent(PAPI_L1_DCM, "PAPI_L1_DCM", "Level 1 data cache misses");
      addevent(PAPI_L1_ICA, "PAPI_L1_ICA", "L1 instruction cache accesses");
      addevent(PAPI_L1_ICH, "PAPI_L1_ICH", "L1 instruction cache hits");
      addevent(PAPI_L1_ICM, "PAPI_L1_ICM", "Level 1 instruction cache misses");
      addevent(PAPI_L1_ICR, "PAPI_L1_ICR", "L1 instruction cache reads");
      addevent(PAPI_L1_LDM, "PAPI_L1_LDM", "Level 1 load misses");
      addevent(PAPI_L1_STM, "PAPI_L1_STM", "Level 1 store misses");
      addevent(PAPI_L1_TCM, "PAPI_L1_TCM", "Level 1 total cache misses");
      addevent(PAPI_L2_DCA, "PAPI_L2_DCA", "L2 D Cache Access");
      addevent(PAPI_L2_DCH, "PAPI_L2_DCH", "L2 D Cache Hit");
      addevent(PAPI_L2_DCM, "PAPI_L2_DCM", "Level 2 data cache misses");
      addevent(PAPI_L2_DCR, "PAPI_L2_DCR", "L2 D Cache Read");
      addevent(PAPI_L2_DCW, "PAPI_L2_DCW", "L2 D Cache Write");
      addevent(PAPI_L2_ICA, "PAPI_L2_ICA", "L2 instruction cache accesses");
      addevent(PAPI_L2_ICH, "PAPI_L2_ICH", "L2 instruction cache hits");
      addevent(PAPI_L2_ICM, "PAPI_L2_ICM", "Level 2 instruction cache misses");
      addevent(PAPI_L2_ICR, "PAPI_L2_ICR", "L2 instruction cache reads");
      addevent(PAPI_L2_LDM, "PAPI_L2_LDM", "Level 2 load misses");
      addevent(PAPI_L2_STM, "PAPI_L2_STM", "Level 2 store misses");
      addevent(PAPI_L2_TCA, "PAPI_L2_TCA", "L2 total cache accesses");
      addevent(PAPI_L2_TCH, "PAPI_L2_TCH", "L2 total cache hits");
      addevent(PAPI_L2_TCM, "PAPI_L2_TCM", "Level 2 total cache misses");
      addevent(PAPI_L2_TCR, "PAPI_L2_TCR", "L2 total cache reads");
      addevent(PAPI_L2_TCW, "PAPI_L2_TCW", "L2 total cache writes");
      addevent(PAPI_L3_DCA, "PAPI_L3_DCA", "L3 D Cache Access");
      addevent(PAPI_L3_DCR, "PAPI_L3_DCR", "L3 D Cache Read");
      addevent(PAPI_L3_DCW, "PAPI_L3_DCW", "L3 D Cache Write");
      addevent(PAPI_L3_ICA, "PAPI_L3_ICA", "L3 instruction cache accesses");
      addevent(PAPI_L3_ICR, "PAPI_L3_ICR", "L3 instruction cache reads");
      addevent(PAPI_L3_LDM, "PAPI_L3_LDM", "Level 3 load misses");
      addevent(PAPI_L3_TCA, "PAPI_L3_TCA", "L3 total cache accesses");
      addevent(PAPI_L3_TCM, "PAPI_L3_TCM", "Level 3 total cache misses");
      addevent(PAPI_L3_TCR, "PAPI_L3_TCR", "L3 total cache reads");
      addevent(PAPI_L3_TCW, "PAPI_L3_TCW", "L3 total cache writes");
      addevent(PAPI_LD_INS, "PAPI_LD_INS", "Load instructions executed");
      addevent(PAPI_LST_INS, "PAPI_LST_INS", "Total load/store inst. executed");
      addevent(PAPI_RES_STL, "PAPI_RES_STL", "Cycles processor is stalled on resource");
      addevent(PAPI_SR_INS, "PAPI_SR_INS", "Store instructions executed");
      addevent(PAPI_TLB_DM, "PAPI_TLB_DM", "Data translation lookaside buffer misses");
      addevent(PAPI_TLB_IM, "PAPI_TLB_IM", "Instr translation lookaside buffer misses");
      addevent(PAPI_TLB_TL, "PAPI_TLB_TL", "Total translation lookaside buffer misses");
      addevent(PAPI_TOT_CYC, "PAPI_TOT_CYC", "Total cycles");
      addevent(PAPI_TOT_IIS, "PAPI_TOT_IIS", "Total instructions issued");
      addevent(PAPI_TOT_INS, "PAPI_TOT_INS", "Total instructions executed");
  }

  /**
   *  For now, this is how we record which single event is being monitored.
   *
   *  NB:  This is an index into the tuple array, not an enum value
   */
  int pmu_papi_t::WhichEvent = 0;

  /**
   *  On system initialization, we need to configure PAPI, set it up for
   *  multithreading, and then check the environment to figure out what events
   *  will be watched
   */
  void pmu_papi_t::onSysInit()
  {
      int ret = PAPI_library_init(PAPI_VER_CURRENT);
      if (ret != PAPI_VER_CURRENT && ret > 0) {
          printf("PAPI library version mismatch!\n");
          exit(1);
      }
      if (ret < 0) {
          printf("Initialization error~\n");
          exit(1);
      }
      // NB: return value is hex of PAPI version (0x4010000)

      ret = PAPI_thread_init(pthread_self);
      if (ret != PAPI_OK) {
          printf("couldn't do thread_init\n");
          exit(1);
      }

      cfg();

      // guess a default configuration, then check env for a better option
      const char* cfg = "PAPI_L1_DCM";
      const char* configstring = getenv("STM_PMU");
      if (configstring)
          cfg = configstring;
      else
          printf("STM_PMU environment variable not found... using %s\n", cfg);

      for (int i = 0; i < num_keys; ++i) {
          if (0 == strcmp(cfg, keystrs[i])) {
              WhichEvent = i;
              break;
          }
      }
      printf("PMU configured using %s (%s)\n", keystrs[WhichEvent], descs[WhichEvent]);
  }

  /**
   *  PAPI wants us to call its shutdown when the app is closing.
   */
  void pmu_papi_t::onSysShutdown()
  {
      PAPI_shutdown();
  }

  /**
   *  For now, a thread will run this to configure its PMU and start counting
   */
  void pmu_papi_t::onThreadInit()
  {
      // [mfs] need to check that the return value is OK
      int ret = PAPI_register_thread();

      if (PAPI_create_eventset(&EventSet) != PAPI_OK) {
          printf("Error calling PAPI_create_eventset\n");
          exit(1);
      }

      // add D1 Misses to the Event Set
      if (PAPI_add_event(EventSet, keys[WhichEvent]) != PAPI_OK) {
          printf("Error adding event %s to eventset\n", keystrs[WhichEvent]);
          exit(1);
      }

      // start counting events in the event set
      if (PAPI_start(EventSet) != PAPI_OK) {
          printf("Error starting EventSet\n");
          exit(1);
      }
  }

  /**
   *  When a thread completes, it calls this to dump its PMU info.
   */
  void pmu_papi_t::onThreadShutdown()
  {
      // shut down counters
      if (PAPI_stop(EventSet, values) != PAPI_OK) {
          printf("Died calling PAPI_stop\n");
          exit(1);
      }
      printf("[PMU %d] : %s=%lld\n", Self->id, keystrs[WhichEvent], values[0]);
      int ret = PAPI_unregister_thread();
      // [mfs] TODO: check return variable?
  }

  /**
   *  We could merge ThreadInit with construction, but then we'd lose symmetry
   *  since we can't match ThreadShutdown with destruction.  Instead, the ctor
   *  just zeros the key fields, and we let the ThreadInit function do the
   *  heavy lifting.
   */
  pmu_papi_t::pmu_papi_t()
      : EventSet(PAPI_NULL)
  {
      for (int i = 0; i < VAL_COUNT; ++i)
          values[i] = 0;
  }
#endif
}
