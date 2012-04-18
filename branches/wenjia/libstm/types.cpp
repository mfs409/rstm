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
 *  In the types/ folder, we have a lot of data structure implementations.  In
 *  some cases, the optimal implementation will have a 'noinline' function that
 *  is rarely called.  To actually ensure that the 'noinline' behavior is
 *  achieved, we put the implementations of those functions here, in a separate
 *  compilation unit.
 */

#include "stm/metadata.hpp"
#include "stm/MiniVector.hpp"
#include "stm/WriteSet.hpp"
#include "stm/UndoLog.hpp"
#include "stm/ValueList.hpp"
#include "policies.hpp"

#ifdef STM_USE_PMU
#include <papi/papi.h>
#endif

namespace
{
  /**
   * We use malloc a couple of times here, and this makes it a bit easier
   */
  template <typename T>
  inline T* typed_malloc(size_t N)
  {
      return static_cast<T*>(malloc(sizeof(T) * N));
  }
}

namespace stm
{
  /**
   * This doubles the size of the index. This *does not* do anything as
   * far as actually doing memory allocation. Callers should delete[] the
   * index table, increment the table size, and then reallocate it.
   */
  inline size_t WriteSet::doubleIndexLength()
  {
      assert(shift != 0 &&
             "ERROR: the writeset doesn't support an index this large");
      shift   -= 1;
      ilength  = 1 << (8 * sizeof(uint32_t) - shift);
      return ilength;
  }

  /***  Writeset constructor.  Note that the version must start at 1. */
  WriteSet::WriteSet(const size_t initial_capacity)
      : index(NULL), shift(8 * sizeof(uint32_t)), ilength(0),
        version(1), list(NULL), capacity(initial_capacity), lsize(0)
  {
      // Find a good index length for the initial capacity of the list.
      while (ilength < 3 * initial_capacity)
          doubleIndexLength();

      index = new index_t[ilength];
      list  = typed_malloc<WriteSetEntry>(capacity);
  }

  /***  Writeset destructor */
  WriteSet::~WriteSet()
  {
      delete[] index;
      free(list);
  }

  /***  Rebuild the writeset */
  void WriteSet::rebuild()
  {
      assert(version != 0 && "ERROR: the version should *never* be 0");

      // extend the index
      delete[] index;
      index = new index_t[doubleIndexLength()];

      for (size_t i = 0; i < lsize; ++i) {
          const WriteSetEntry& l = list[i];
          size_t h = hash(l.addr);

          // search for the next available slot
          while (index[h].version == version)
              h = (h + 1) % ilength;

          index[h].address = l.addr;
          index[h].version = version;
          index[h].index   = i;
      }
  }

  /***  Resize the writeset */
  void WriteSet::resize()
  {
      WriteSetEntry* temp  = list;
      capacity     *= 2;
      list          = typed_malloc<WriteSetEntry>(capacity);
      memcpy(list, temp, sizeof(WriteSetEntry) * lsize);
      free(temp);
  }

  /***  Another writeset reset function that we don't want inlined */
  void WriteSet::reset_internal()
  {
      memset(index, 0, sizeof(index_t) * ilength);
      version = 1;
  }

  /**
   * Deal with the actual rollback of log entries, which depends on the
   * STM_ABORT_ON_THROW configuration as well as on the type of write logging
   * we're doing.
   */
#if defined(STM_ABORT_ON_THROW)
  void WriteSet::rollback(void** exception, size_t len)
  {
      // early exit if there's no exception
      if (!len)
          return;

      // for each entry, call rollback with the exception range, which will
      // actually writeback if the entry is in the address range.
      void** upper = (void**)((uint8_t*)exception + len);
      for (iterator i = begin(), e = end(); i != e; ++i)
          i->rollback(exception, upper);
  }
#else
  // rollback was inlined
#endif

#if !defined(STM_ABORT_ON_THROW)
  void UndoLog::undo()
  {
      for (iterator i = end() - 1, e = begin(); i >= e; --i)
          i->undo();
  }
#else
  void UndoLog::undo(void** exception, size_t len)
  {
      // don't undo the exception object, if it happens to be logged, also
      // don't branch on the inner loop if there isn't an exception
      //
      // for byte-logging we need to deal with the mask to see if the write
      // is going to be in the exception range
      if (!exception) {  // common case only adds one branch
          for (iterator i = end() - 1, e = begin(); i >= e; --i)
              i->undo();
          return;
      }

      void** upper = (void**)((uint8_t*)exception + len);
      for (iterator i = end() - 1, e = begin(); i >= e; --i) {
          if (i->filter(exception, upper))
              continue;
          i->undo();
      }
  }
#endif

  /**
   * We outline the slowpath filter. If this /ever/ happens it will be such a
   * corner case that it just doesn't matter. Plus this is an abort path
   * anyway... consider it a contention management technique.
   */
  bool ByteLoggingUndoLogEntry::filterSlow(void** lower, void** upper)
  {
      // we have some sort of intersection... we start by assuming that it's
      // total.
      if (addr >= lower && addr + 1 < upper)
          return true;

      // We have a complicated intersection. We'll do a really slow loop
      // through each byte---at this point it doesn't make a difference.
      for (unsigned i = 0; i < sizeof(val); ++i) {
          void** a = (void**)(byte_addr + i);
          if (a >= lower && a < upper)
              byte_mask[i] = 0x0;
      }

      // did we filter every byte?
      return (mask == 0x0);
  }

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

} // namespace stm
