/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <setjmp.h>
#include <iostream>
#include "txthread.hpp"
#include "policies.hpp"
#include "algs/tml_inline.hpp"
#include "algs.hpp"
#include "inst.hpp"

using namespace stm;

namespace
{
  /**
   *  The name of the algorithm with which libstm was initialized
   */
  const char* init_lib_name;
} // (anonymous namespace)

namespace stm
{
#ifndef STM_CHECKPOINT_ASM
  /**
   *  The default mechanism that libstm uses for an abort. An API environment
   *  may also provide its own abort mechanism (see itm2stm for an example of
   *  how the itm shim does this).
   *
   *  This is ugly because rollback has a configuration-dependent signature.
   */
  NORETURN void TxThread::tmabort()
  {
      stm::TxThread* tx = stm::Self;
#if defined(STM_ABORT_ON_THROW)
      TxThread::tmrollback(tx, NULL, 0);
#else
      TxThread::tmrollback(tx);
#endif
      jmp_buf* scope = (jmp_buf*)tx->checkpoint;
      longjmp(*scope, 1);
  }
#endif

  /*** BACKING FOR GLOBAL VARS DECLARED IN TXTHREAD.HPP */
  pad_word_t threadcount          = {0, {0}}; // thread count
  TxThread*  threads[MAX_THREADS] = {0}; // all TxThreads
  THREAD_LOCAL_DECL_TYPE(TxThread*) Self; // this thread's TxThread

  /**
   *  Constructor sets up the lists and vars
   */
  TxThread::TxThread()
      : nesting_depth(0), in_tx(0),
#ifndef STM_CHECKPOINT_ASM
        checkpoint(NULL),
#endif
        allocator(),
        num_commits(0), num_aborts(0), num_restarts(0),
        num_ro(0),
#ifdef STM_STACK_PROTECT
        stack_high(NULL),
        stack_low(~0x0),
#endif
        start_time(0), tmlHasLock(false), undo_log(64), vlist(64), writes(64),
        r_orecs(64), locks(64),
        wf((filter_t*)FILTER_ALLOC(sizeof(filter_t))),
        rf((filter_t*)FILTER_ALLOC(sizeof(filter_t))),
        prio(0), consec_aborts(0), seed((unsigned long)&id), myRRecs(64),
        order(-1), alive(1),
        r_bytelocks(64), w_bytelocks(64), r_bitlocks(64), w_bitlocks(64),
        my_mcslock(new mcs_qnode_t()),
        cm_ts(INT_MAX),
        cf((filter_t*)FILTER_ALLOC(sizeof(filter_t))),
        nanorecs(64), begin_wait(0), strong_HG(),
        irrevocable(false), status(0), r_addrs(64), turn(),
        node(),nn(0),read_only(false),progress_is_seen(false),
        last_val_time((uint64_t)-1),
        pmu()
  {
      // prevent new txns from starting.
      while (true) {
          int i = curr_policy.ALG_ID;
          if (bcasptr(&tmbegin, stms[i].begin, &begin_blocker))
              break;
          spin64();
      }

      // set my pointers
      my_tmread = (void**)&tmread;
      my_tmwrite = (void**)&tmwrite;
      my_tmcommit = (void**)&tmcommit;

      // We need to be very careful here.  Some algorithms (at least TLI and
      // NOrecPrio) like to let a thread look at another thread's TxThread
      // object, even when that other thread is not in a transaction.  We
      // don't want the TxThread object we are making to be visible to
      // anyone, until it is 'ready'.
      //
      // Since those algorithms can only find this TxThread object by looking
      // in threads[], and they scan threads[] by using threadcount.val, we
      // use the following technique:
      //
      // First, only this function can ever change threadcount.val.  It does
      // not need to do so atomically, but it must do so from inside of the
      // critical section created by the begin_blocker CAS
      //
      // Second, we can predict threadcount.val early, but set it late.  Thus
      // we can completely configure this TxThread, and even put it in the
      // threads[] array, without writing threadcount.val.
      //
      // Lastly, when we finally do write threadcount.val, we make sure to
      // preserve ordering so that write comes after initialization, but
      // before lock release.

      // predict the new value of threadcount.val
      id = threadcount.val + 1;

      // update the allocator
      allocator.setID(id-1);

      // set up my lock word
      my_lock.fields.lock = 1;
      my_lock.fields.id = id;

      // clear filters
      wf->clear();
      rf->clear();

      // configure my TM instrumentation
      install_algorithm_local(curr_policy.ALG_ID);

      // set the pointer to this TxThread
      threads[id-1] = this;

      // set the epoch to default
      epochs[id-1].val = EPOCH_MAX;

      // configure the pmu
      pmu.onThreadInit();

      // NB: at this point, we could change the mode based on the thread
      //     count.  The best way to do so would be to install ProfileTM.  We
      //     would need to be very careful, though, in case another thread is
      //     already running ProfileTM.  We'd also need a way to skip doing
      //     so if a non-adaptive policy was in place.  An even better
      //     strategy might be to put a request for switching outside the
      //     critical section, as the last line of this method.
      //
      // NB: For the release, we are omitting said code, as it does not
      //     matter in the workloads we provide.  We should revisit at some
      //     later time.

      // now publish threadcount.val
      CFENCE;
      threadcount.val = id;

      // now we can let threads progress again
      CFENCE;
      tmbegin = stms[curr_policy.ALG_ID].begin;
  }

  /*** print a message and die */
  void UNRECOVERABLE(const char* msg)
  {
      std::cerr << msg << std::endl;
      exit(-1);
  }

  /***  GLOBAL FUNCTION POINTERS FOR OUR INDIRECTION-BASED MODE SWITCHING */

  /**
   *  The begin function pointer.  Note that we need tmbegin to equal
   *  begin_cgl initially, since "0" is the default algorithm
   */
  void (*volatile tmbegin)() = begin_CGL;

  /**
   *  The tmrollback and tmirrevoc pointers
   */
  void (*TxThread::tmrollback)(STM_ROLLBACK_SIG(,,));
  bool (*TxThread::tmirrevoc)(TxThread*) = NULL;

  /*** the init factory */
  void TxThread::thread_init()
  {
      // multiple inits from one thread do not cause trouble
      if (Self) return;

      // create a TxThread and save it in thread-local storage
      Self = new TxThread();
  }

  /*** shut down a thread */
  void TxThread::thread_shutdown()
  {
      // for now, all we need to do is dump the PMU information
      Self->pmu.onThreadShutdown();
  }

  /**
   *  Simplified support for self-abort
   */
  void restart()
  {
      // get the thread's tx context
      TxThread* tx = Self;
      // register this restart
      ++tx->num_restarts;
      // call the abort code
      tx->tmabort();
  }


  /**
   *  When the transactional system gets shut down, we call this to dump stats
   */
  void sys_shutdown()
  {
      static volatile unsigned int mtx = 0;
      while (!bcas32(&mtx, 0u, 1u)) { }

      uint64_t nontxn_count = 0;                // time outside of txns
      uint32_t pct_ro       = 0;                // read only txn ratio
      uint32_t txn_count    = 0;                // total txns
      uint32_t rw_txns      = 0;                // rw commits
      uint32_t ro_txns      = 0;                // ro commits
      for (uint32_t i = 0; i < threadcount.val; i++) {
          std::cout << "Thread: "       << threads[i]->id
                    << "; RW Commits: " << threads[i]->num_commits
                    << "; RO Commits: " << threads[i]->num_ro
                    << "; Aborts: "     << threads[i]->num_aborts
                    << "; Restarts: "   << threads[i]->num_restarts
                    << std::endl;
          threads[i]->abort_hist.dump();
          rw_txns += threads[i]->num_commits;
          ro_txns += threads[i]->num_ro;
          nontxn_count += threads[i]->total_nontxn_time;
      }
      txn_count = rw_txns + ro_txns;
      pct_ro = (!txn_count) ? 0 : (100 * ro_txns) / txn_count;

      std::cout << "Total nontxn work:\t" << nontxn_count << std::endl;

      // if we ever switched to ProfileApp, then we should print out the
      // ProfileApp custom output.
      if (app_profiles) {
          uint32_t divisor =
              (curr_policy.ALG_ID == ProfileAppAvg) ? txn_count : 1;
          if (divisor == 0)
              divisor = 0u - 1u; // unsigned infinity :)

          std::cout << "# " << stms[curr_policy.ALG_ID].name << " #" << std::endl;
          std::cout << "# read_ro, read_rw_nonraw, read_rw_raw, write_nonwaw, write_waw, txn_time, "
                    << "pct_txtime, roratio #" << std::endl;
          std::cout << app_profiles->read_ro  / divisor << ", "
                    << app_profiles->read_rw_nonraw / divisor << ", "
                    << app_profiles->read_rw_raw / divisor << ", "
                    << app_profiles->write_nonwaw / divisor << ", "
                    << app_profiles->write_waw / divisor << ", "
                    << app_profiles->txn_time / divisor << ", "
                    << ((100*app_profiles->timecounter)/(nontxn_count+1)) << ", "
                    << pct_ro << " #" << std::endl;
      }

      // dump PMU information, if any
      pmu_t::onSysShutdown();

      CFENCE;
      mtx = 0;
  }

  /**
   *  for parsing input to determine the valid algorithms for a phase of
   *  execution.
   *
   *  Setting a policy is a lot like changing algorithms, but requires a little
   *  bit of custom synchronization
   */
  void set_policy(const char* phasename)
  {
      // prevent new txns from starting.  Note that we can't be in ProfileTM
      // while doing this
      while (true) {
          int i = curr_policy.ALG_ID;
          if (i == ProfileTM)
              continue;
          if (bcasptr(&tmbegin, stms[i].begin, &begin_blocker))
              break;
          spin64();
      }

      // wait for everyone to be out of a transaction (scope == NULL)
      for (unsigned i = 0; i < threadcount.val; ++i)
          while (threads[i]->in_tx)
              spin64();

      // figure out the algorithm for the STM, and set the adapt policy

      // we assume that the phase is a single-algorithm phase
      int new_algorithm = stm_name_map(phasename);
      int new_policy = Single;
      if (new_algorithm == -1) {
          int tmp = pol_name_map(phasename);
          if (tmp == -1)
              UNRECOVERABLE("Invalid configuration string");
          new_policy = tmp;
          new_algorithm = pols[tmp].startmode;
      }

      curr_policy.POL_ID = new_policy;
      curr_policy.waitThresh = pols[new_policy].waitThresh;
      curr_policy.abortThresh = pols[new_policy].abortThresh;

      // install the new algorithm
      install_algorithm(new_algorithm, Self);
  }

  /**
   *  Template Metaprogramming trick for initializing all STM algorithms.
   *
   *  This is either a very gross trick, or a very cool one.  We have ALG_MAX
   *  algorithms, and they all need to be initialized.  Each has a unique
   *  identifying integer, and each is initialized by calling an instantiation
   *  of initTM<> with that integer.
   *
   *  Rather than call each function through a line of code, we use a
   *  tail-recursive template: When we call MetaInitializer<0>.init(), it will
   *  recursively call itself for every X, where 0 <= X < ALG_MAX.  Since
   *  MetaInitializer<X>::init() calls initTM<X> before recursing, this
   *  instantiates and calls the appropriate initTM function.  Thus we
   *  correctly call all initialization functions.
   *
   *  Furthermore, since the code is tail-recursive, at -O3 g++ will inline all
   *  the initTM calls right into the sys_init function.  While the code is not
   *  performance critical, it's still nice to avoid the overhead.
   */
  template <int I = 0>
  struct MetaInitializer
  {
      /*** default case: init the Ith tm, then recurse to I+1 */
      static void init()
      {
          initTM<(stm::ALGS)I>();
          MetaInitializer<(stm::ALGS)I+1>::init();
      }
  };
  template <>
  struct MetaInitializer<ALG_MAX>
  {
      /*** termination case: do nothing for ALG_MAX */
      static void init() { }
  };

  /**
   *  Initialize the TM system.
   */
  void sys_init()
  {
      static volatile uint32_t mtx = 0;

      if (bcas32(&mtx, 0u, 1u)) {
          // manually register all behavior policies that we support.  We do
          // this via tail-recursive template metaprogramming
          MetaInitializer<0>::init();

          // guess a default configuration, then check env for a better option
          const char* cfg = "NOrec";
          const char* configstring = getenv("STM_CONFIG");
          if (configstring)
              cfg = configstring;
          else
              printf("STM_CONFIG environment variable not found... using %s\n", cfg);
          init_lib_name = cfg;

          // now initialize the the adaptive policies
          pol_init();

          // this is (for now) how we make sure we have a buffer to hold
          // profiles.  This also specifies how many profiles we do at a time.
          char* spc = getenv("STM_NUMPROFILES");
          if (spc != NULL)
              profile_txns = strtol(spc, 0, 10);
          profiles = (dynprof_t*)malloc(profile_txns * sizeof(dynprof_t));
          for (unsigned i = 0; i < profile_txns; i++)
              profiles[i].clear();

          // now set the phase
          set_policy(cfg);

          // and configure the PMU interface
          pmu_t::onSysInit();

          printf("STM library configured using %s\n", cfg);

          mtx = 2;
      }
      while (mtx != 2) { }
  }

  /**
   *  Return the name of the algorithm with which the library was configured
   */
  const char* get_algname()
  {
      return init_lib_name;
  }

  /**
   * The function pointers:
   */
  THREAD_LOCAL_DECL_TYPE(TM_FASTCALL void(*tmcommit)());
  THREAD_LOCAL_DECL_TYPE(TM_FASTCALL void*(*tmread)(STM_READ_SIG(,)));
  THREAD_LOCAL_DECL_TYPE(TM_FASTCALL void(*tmwrite)(STM_WRITE_SIG(,,)));

#ifndef STM_CHECKPOINT_ASM
  /**
   *  Code to start a transaction.  We assume the caller already performed a
   *  setjmp, and is passing a valid setjmp buffer to this function.
   *
   *  The code to begin a transaction *could* all live on the far side of a
   *  function pointer.  By putting some of the code into this inlined
   *  function, we can:
   *
   *    (a) avoid overhead under subsumption nesting and
   *    (b) avoid code duplication or MACRO nastiness
   */
  void begin(scope_t* s, uint32_t /*abort_flags*/)
  {
      TxThread* tx = stm::Self;
      if (++tx->nesting_depth > 1)
          return;

      // we must ensure that the write of the transaction's scope occurs
      // *before* the read of the begin function pointer.  On modern x86, a
      // CAS is faster than using WBR or xchg to achieve the ordering.  On
      // SPARC, WBR is best.
      tx->checkpoint = s;
#ifdef STM_CPU_SPARC
      tx->in_tx = 1;
      WBR;
#else
      (void)casptr(&tx->in_tx, 0, 1);
#endif

      // some adaptivity mechanisms need to know nontransactional and
      // transactional time.  This code suffices, because it gets the time
      // between transactions.  If we need the time for a single transaction,
      // we can run ProfileTM
      if (tx->end_txn_time)
          tx->total_nontxn_time += (tick() - tx->end_txn_time);

      // now call the per-algorithm begin function
      tmbegin();
  }
#endif

  /**
   *  Code to commit a transaction.  As in begin(), we are using forced
   *  inlining to save a little bit of overhead for subsumption nesting, and to
   *  prevent code duplication.
   */
  void commit()
  {
      TxThread* tx = stm::Self;
      // don't commit anything if we're nested... just exit this scope
      if (--tx->nesting_depth)
          return;

      // dispatch to the appropriate end function
      tmcommit();

      // indicate "not in tx"
      CFENCE;
      tx->in_tx = 0;

      // record start of nontransactional time
      tx->end_txn_time = tick();
  }

  /**
   *  get a chunk of memory that will be automatically reclaimed if the caller
   *  is a transaction that ultimately aborts
   */
  void* tx_alloc(size_t size) { return Self->allocator.txAlloc(size); }

  /**
   *  Free some memory.  If the caller is a transaction that ultimately aborts,
   *  the free will not happen.  If the caller is a transaction that commits,
   *  the free will happen at commit time.
   */
  void tx_free(void* p) { Self->allocator.txFree(p); }

  /***  Set up a thread's transactional context */
  void thread_init() { TxThread::thread_init(); }

  /***  Shut down a thread's transactional context */
  void thread_shutdown() { TxThread::thread_shutdown(); }

  /*** declare the next transaction of this thread to be read-only */
  void declare_read_only()
  {
    stm::TxThread* tx = (stm::TxThread*)stm::Self;
    if (tx->nesting_depth == 0)
        tx->read_only = true;
  }

  /** test function*/
  void* get_tls()
  {
    stm::TxThread* tx = stm::Self;
    return tx;
  }
} // namespace stm
