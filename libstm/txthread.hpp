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
 *  Definitions for a common environment for all our STM implementations.
 *  The TxThread object holds all the metadata that a thread needs.
 *
 *  In addition, this file declares the thread-local pointer that a thread
 *  can use to access its TxThread object.
 */

#ifndef TXTHREAD_HPP__
#define TXTHREAD_HPP__

#include "../alt-license/rand_r_32.h"
#include "../include/ThreadLocal.hpp"
#include "../include/abstract_compiler.hpp"
#include "Orecs.hpp"
#include "ByteLocks.hpp"
#include "BitLocks.hpp"
#include "RRecs.hpp"
#include "BitFilter.hpp"
#include "BitFilter64.hpp"
#include "SpinLocks.hpp"
#include "WriteSet.hpp"
#include "UndoLog.hpp"
#include "ValueList.hpp"
#include "WBMMPolicy.hpp"
#include "Toxic.hpp"
#include "PMU.hpp"
#include "SimpleQueue.hpp"

#ifdef STM_CHECKPOINT_ASM
#include "checkpoint.hpp"
#endif

#ifdef STM_HAS_AOU
#include "../include/aou.h"
#endif

namespace stm
{
  /**
   *  The TxThread struct holds all of the metadata that a thread needs in
   *  order to use any of the STM algorithms we support.
   *
   *  NB: the order of fields has not been rigorously studied.  It is very
   *      likely that a better order would improve performance.
   *
   *  [mfs] I'm pretty sure we are generating performance bugs in 64-bit code
   *        by having 32-bit fields in this struct.  They are likely to be
   *        causing unaligned 64-bit accesses, some of which will surely span
   *        a cache line.
   */
  struct TxThread
  {
      /**
       * THESE FIELDS MUST NOT BE MOVED.  THEY MUST BE IN THIS ORDER OR THE
       * CUSTOM ASM IN CHECKPOINT.S WILL BREAK
       */
#ifdef STM_CHECKPOINT_ASM
      uint32_t       nesting_depth; // nesting
      volatile bool  in_tx;         // flag for if we are in a transaction
      checkpoint_t   checkpoint;    // used to roll back
#else
      uint32_t       nesting_depth; // nesting; 0 == not in transaction
      volatile bool  in_tx;         // flag for if we are in a transaction
      scope_t*       checkpoint;    // used to roll back
#endif

      /*** THESE FIELDS DEAL WITH THE STM IMPLEMENTATIONS ***/
      uint32_t       id;            // per thread id
      WBMMPolicy     allocator;     // buffer malloc/free
      uint32_t       num_commits;   // stats counter: commits
      uint32_t       num_aborts;    // stats counter: aborts
      uint32_t       num_restarts;  // stats counter: restart()s
      uint32_t       num_ro;        // stats counter: read-only commits
      uint32_t       num_temp;      // [wer210]stats counter: temp use
#ifdef STM_PROTECT_STACK
      void**         stack_high;    // the stack pointer at begin_tx time
      void**         stack_low;     // norec stack low-water mark
#endif
      volatile uintptr_t      start_time;    // start time of transaction
      volatile uintptr_t      end_time;      // end time of txransaction
      uintptr_t      ts_cache;      // last validation time
      bool           tmlHasLock;    // is tml thread holding the lock
      UndoLog        undo_log;      // etee undo log
      ValueList      vlist;         // NOrec read log
      WriteSet       writes;        // write set
      OrecList       r_orecs;       // read set for orec STMs
      OrecList       locks;         // list of all locks held by tx
      id_version_t   my_lock;       // lock word for orec STMs
      filter_t*      wf;            // write filter
      filter_t*      rf;            // read filter
      volatile uint32_t prio;       // for priority
      uint32_t       consec_aborts; // count consec aborts
      uint32_t       seed;          // for randomized backoff
      RRecList       myRRecs;       // indices of rrecs I set
      intptr_t       order;         // for stms that order txns eagerly
      volatile uint32_t alive;      // for STMs that allow remote abort
      ByteLockList   r_bytelocks;   // list of all byte locks held for read
      ByteLockList   w_bytelocks;   // all byte locks held for write
      BitLockList    r_bitlocks;    // list of all bit locks held for read
      BitLockList    w_bitlocks;    // list of all bit locks held for write
      mcs_qnode_t*   my_mcslock;    // for MCS
      uintptr_t      valid_ts;      // the validation timestamp for each tx
      uintptr_t      cm_ts;         // the contention manager timestamp
      filter_t*      cf;            // conflict filter (RingALA)
      NanorecList    nanorecs;      // list of nanorecs held
      uint32_t       consec_commits;// count consec commits
      uint32_t       consec_ro;     // count consec ro commits
      toxic_t        abort_hist;    // for counting poison
      uint32_t       begin_wait;    // how long did last tx block at begin
      bool           strong_HG;     // for strong hourglass
      bool           irrevocable;   // tells begin_blocker that I'm THE ONE

      /*** FOR COHORTS */
      volatile uintptr_t status;    // tx status
      AddressList r_addrs;          // tx read addresses
      cohorts_node_t turn;          // tx turn node

      /*** FOR COHORTS WITH EARLY SEAL */
      int32_t cohort_reads;
      int32_t cohort_writes;
      int32_t cohort_aborts;

      /*** FOR CTOKENQ */
      cohorts_node_t node[2];         // tx turn node[2]
      uint32_t nn;                    // tx node number

      /*** FOR PESSIMISTIC */
      bool read_only;               // mark a transaction to be read-only txn
      bool progress_is_seen;        // for recording waiting progress

      /*** FOR ELA via x86 tick() */
      volatile uint64_t last_val_time; // time of last validation

      /*** PER-THREAD FIELDS FOR ENABLING ADAPTIVITY POLICIES */
      uint64_t      end_txn_time;      // end of non-transactional work
      uint64_t      total_nontxn_time; // time on non-transactional work
      pmu_t         pmu;               // for accessing the hardware PMU


       bool irrevoc;                // if true, start in software mode, else
                    // hardware mode
       /*** FOR HyOne ****/
       uint32_t     hyOne_abort_count;  //the abort count for HyOne

      /*** AOU ONLY: need a context for the AOU alert handler ***/
#if defined(STM_HAS_AOU)
      Watch_Descriptor* aou_context;            // this is the AOU descriptor
#endif

      /*** INSTRUMENTATION-RELATED FIELDS ***/
#if defined(STM_INST_FINEGRAINADAPT)
      // these are the addresses of the thread's function pointers, which
      // allow another thread to change this thread's instrumentation (e.g.,
      // on mode switch)
      void** my_tmcommit;
      void** my_tmread;
      void** my_tmwrite;
#elif defined(STM_INST_COARSEGRAINADAPT) || defined(STM_INST_SWITCHADAPT) || defined(STM_INST_ONESHOT)
      // the transaction state just depends on this field
      uint32_t      mode; // MODE_TURBO, MODE_WRITE, or MODE_RO
#else
#error "Unable to determine instrumentation mode"
#endif

      /**
       *  For shutting down threads.
       *
       * [mfs] Why is this still a static member?
       */
      static void thread_shutdown();

      /**
       * the init factory.  Construction of TxThread objects is only possible
       * through this function.  Note, too, that destruction is forbidden.
       *
       * [mfs] Why is this still a static member?  TxThread is not externally
       *       visible anymore, so we could just make its constructor
       *       public, and then have a regular function for thread_init.
       */
      static void thread_init();
    protected:
      TxThread();
      ~TxThread() { }
  }; // class TxThread

} // namespace stm

#endif // TXTHREAD_HPP__
