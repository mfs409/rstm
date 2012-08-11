/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef PMU_HPP__
#define PMU_HPP__
namespace stm
{
  /**
   *  This is for providing an interface to the PMU (currently via PAPI) in
   *  order to measure low-level hardware events that occur during transaction
   *  execution.
   *
   *  [mfs] Move to PMU.hpp?
   */
  struct pmu_papi_t
  {
      static const int VAL_COUNT = 8;
      int EventSet;
      long long values[VAL_COUNT];
      static int WhichEvent;
      static void onSysInit();
      static void onSysShutdown();
      void onThreadInit();
      void onThreadShutdown();
      pmu_papi_t();
  };

  /**
   *  When STM_USE_PMU is not set, we don't do anything for these
   */
  struct pmu_nop_t
  {
      static void onSysInit()     { }
      static void onSysShutdown() { }
      void onThreadInit()         { }
      void onThreadShutdown()     { }
      pmu_nop_t()                 { }
  };

#ifdef STM_USE_PMU
  typedef pmu_papi_t pmu_t;
#else
  typedef pmu_nop_t pmu_t;
#endif
} // namespace stm

#endif // PMU_HPP__
