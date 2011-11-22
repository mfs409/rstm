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
#include "stm/txthread.hpp"
#include "stm/lib_globals.hpp"
#include "algs/algs.hpp"
#include "algs/tml_inline.hpp"
#include "policies/policies.hpp"
#include "sandboxing/sandboxing.hpp"
#include "inst.hpp"

using namespace stm;
using std::cout;

/**
 *  The name of the algorithm with which libstm was initialized, and the abi
 *  routine used to get it.
 */
static const char* init_lib_name = NULL;

const char*
stm::get_algname()
{
    return init_lib_name;
}

/**
 *  The default mechanism that libstm uses for an abort. An API environment
 *  may also provide its own abort mechanism (see itm2stm for an example of
 *  how the itm shim does this).
 *
 *  This is ugly because rollback has a configuration-dependent signature.
 */
static void default_abort_handler(TxThread* tx) NORETURN;
void
default_abort_handler(TxThread* tx)
{
    jmp_buf* scope = (jmp_buf*)TxThread::tmrollback(tx
#if defined(STM_ABORT_ON_THROW)
                                                    , NULL, 0
#endif
                                                   );
    // need to null out the scope
    siglongjmp(*scope, 1);
}

/**
 *  To initialize an algorithm we need to call initTM<ALGS>() where ALGS is
 *  the enum in algs/algs.hpp. This uses template metaptrogramming to do that.
 */
template <int I>
static void init_algorithm() {
    initTM<static_cast<stm::ALGS>(I)>();
    init_algorithm<I+1>();
}

template <>
void init_algorithm<ALG_MAX>() {
}

static void init_algorithms() {
    init_algorithm<0>();
}

/*** BACKING FOR GLOBAL VARS DECLARED IN TXTHREAD.HPP */
pad_word_t stm::threadcount          = {0}; // thread count
TxThread*  stm::threads[MAX_THREADS] = {0}; // all TxThreads
THREAD_LOCAL_DECL_TYPE(TxThread*) stm::Self; // this thread's TxThread

/**
 *  Constructor sets up the lists and vars
 */
TxThread::TxThread()
    : id(0),
      nesting_depth(0),
      allocator(),
      num_commits(0),
      num_aborts(0),
      num_restarts(0),
      num_ro(0),
      scope(NULL),
      stack_high(NULL),
      stack_low((void**)~0x0),
      start_time(0),
      end_time(0),
      ts_cache(0),
      tmlHasLock(false),
      undo_log(64),
      vlist(64),
      writes(64),
      r_orecs(64),
      locks(64),
      wf((filter_t*)FILTER_ALLOC(sizeof(filter_t))),
      rf((filter_t*)FILTER_ALLOC(sizeof(filter_t))),
      prio(0),
      consec_aborts(0),
      seed((unsigned long)&id),
      myRRecs(64),
      order(-1),
      alive(1),
      r_bytelocks(64),
      w_bytelocks(64),
      r_bitlocks(64),
      w_bitlocks(64),
      my_mcslock(new mcs_qnode_t()),
      valid_ts(0),
      cm_ts(INT_MAX),
      cf((filter_t*)FILTER_ALLOC(sizeof(filter_t))),
      nanorecs(64),
      consec_commits(0),
      abort_hist(),
      begin_wait(0),
      strong_HG(),
      irrevocable(false),
      end_txn_time(0),
      total_nontxn_time(0),
      pthreadid(),
      tmcommit(NULL),
      tmread(NULL),
      tmwrite(NULL)
{
    pthreadid = pthread_self();

    // prevent new txns from starting.
    while (true) {
        int i = curr_policy.ALG_ID;
        if (sync_bcas(&tmbegin, stms[i].begin, &begin_blocker))
            break;
        spin64();
    }

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
    id_version_t lock;
    lock.fields.lock = 1;
    lock.fields.id = id;
    my_lock = lock.all;

    // clear filters
    wf->clear();
    rf->clear();

    // configure my TM instrumentation
    install_algorithm_local(curr_policy.ALG_ID, this);

    // set the pointer to this TxThread
    threads[id-1] = this;

    // set the epoch to default
    epochs[id-1].val = EPOCH_MAX;

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
void stm::UNRECOVERABLE(const char* msg)
{
    std::cerr << msg << std::endl;
#ifdef NDEBUG
    exit(-1);
#else
    assert(false);
#endif
}

/***  GLOBAL FUNCTION POINTERS FOR OUR INDIRECTION-BASED MODE SWITCHING */

/**
 *  The begin function pointer.  Note that we need tmbegin to equal
 *  begin_cgl initially, since "0" is the default algorithm
 */
bool TM_FASTCALL (*volatile TxThread::tmbegin)(TxThread*) = begin_CGL;

/**
 *  The tmrollback, tmabort, tmirrevoc, and tmvalidate pointers
 */
scope_t* (*TxThread::tmrollback)(STM_ROLLBACK_SIG(,,));
NORETURN void (*TxThread::tmabort)(TxThread*) = default_abort_handler;
bool (*TxThread::tmirrevoc)(TxThread*) = NULL;
bool (*TxThread::tmvalidate)(TxThread*) = NULL;

/*** the init factory */
void
TxThread::thread_init()
{
    // multiple inits from one thread do not cause trouble
    if (Self) return;

    // create a TxThread and save it in thread-local storage
    Self = new TxThread();

    //
    sandbox::init_thread();
}

/**
 *  Simplified support for self-abort
 */
void
stm::restart()
{
    // get the thread's tx context
    TxThread* tx = Self;
    // register this restart
    ++tx->num_restarts;
    // call the abort code
    tx->tmabort(tx);
}

/**
 *  When the transactional system gets shut down, we call this to dump stats
 */
void
stm::sys_shutdown()
{
    static volatile int lock = 0;
    while (!sync_bcas(&lock, 0, 1))
        ;

    uint64_t nontxn_count = 0;                // time outside of txns
    uint32_t pct_ro       = 0;                // read only txn ratio
    uint32_t txn_count    = 0;                // total txns
    uint32_t rw_txns      = 0;                // rw commits
    uint32_t ro_txns      = 0;                // ro commits

    for (uint32_t i = 0; i < threadcount.val; i++) {
        TxThread& tx = *threads[i];
        cout << "Thread: "       << tx.id
             << "; RW Commits: " << tx.num_commits
             << "; RO Commits: " << tx.num_ro
             << "; Aborts: "     << tx.num_aborts
             << "; Restarts: "   << tx.num_restarts << "\n";
        tx.abort_hist.dump();
        rw_txns += tx.num_commits;
        ro_txns += tx.num_ro;
        nontxn_count += tx.total_nontxn_time;
    }

    txn_count = rw_txns + ro_txns;
    pct_ro = (!txn_count) ? 0 : (100 * ro_txns) / txn_count;

    cout << "Total nontxn work:\t" << nontxn_count << "\n";

    // if we ever switched to ProfileApp, then we should print out the
    // ProfileApp custom output.
    if (!app_profiles) {
        CFENCE;
        lock = 0;
        return;
    }

    uint32_t div = (curr_policy.ALG_ID == ProfileAppAvg) ? txn_count : 1;
    div = (div) ? : 0u - 1u; // unsigned infinity :)

    cout << "# " << stms[curr_policy.ALG_ID].name << " #\n"
         << "# read_ro, read_rw_nonraw, read_rw_raw, write_nonwaw, "
         << "write_waw, txn_time, pct_txtime, roratio #\n"
         << app_profiles->read_ro  / div << ", "
         << app_profiles->read_rw_nonraw / div << ", "
         << app_profiles->read_rw_raw / div << ", "
         << app_profiles->write_nonwaw / div << ", "
         << app_profiles->write_waw / div << ", "
         << app_profiles->txn_time / div << ", "
         << ((100*app_profiles->timecounter)/nontxn_count) << ", "
         << pct_ro << " #\n";

    CFENCE;
    lock = 0;
}

/**
 *  for parsing input to determine the valid algorithms for a phase of
 *  execution.
 *
 *  Setting a policy is a lot like changing algorithms, but requires a little
 *  bit of custom synchronization
 */
void
stm::set_policy(const char* phasename)
{
    // prevent new txns from starting.  Note that we can't be in ProfileTM
    // while doing this
    while (true) {
        int i = curr_policy.ALG_ID;
        if (i == ProfileTM)
            continue;
        if (sync_bcas(&TxThread::tmbegin, stms[i].begin, &begin_blocker))
            break;
        spin64();
    }

    // wait for everyone to be out of a transaction (scope == NULL)
    for (unsigned i = 0; i < threadcount.val; ++i)
        while (threads[i]->scope)
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
 *  Initialize the TM system.
 */
void
stm::sys_init(AbortHandler conflict_abort_handler)
{
    static volatile uint32_t lock = 0;

    // only one thread should get through... everyone else just waits.
    if (!sync_bcas(&lock, 0u, 1u)) {
        while (lock != 2)
            ;
        return;
    }

    sandbox::init_system();
    init_algorithms();

    // check env for a default
    init_lib_name = getenv("STM_CONFIG");
    if (!init_lib_name) {
        init_lib_name = "NOrec";
        cout << "STM_CONFIG environment variable not found... using "
             << init_lib_name << "\n";
    }

    // now initialize the the adaptive policies
    pol_init(init_lib_name);

    // this is (for now) how we make sure we have a buffer to hold
    // profiles.  This also specifies how many profiles we do at a time.
    char* spc = getenv("STM_NUMPROFILES");
    if (spc != NULL)
        profile_txns = strtol(spc, 0, 10);
    profiles = typed_malloc<dynprof_t>(profile_txns);
    for (unsigned i = 0; i < profile_txns; i++)
        profiles[i].clear();

    // Initialize the global abort handler.
    TxThread::tmabort = (conflict_abort_handler) ? : default_abort_handler;

    // now set the phase
    set_policy(init_lib_name);

    cout << "STM library configured using config == " << init_lib_name << "\n";

    CFENCE;
    lock = 2;
}
