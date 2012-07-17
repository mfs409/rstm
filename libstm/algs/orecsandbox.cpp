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
 *  SandboxOrecELA Implementation
 *
 *    This is similar to the Detlefs algorithm for privatization-safe STM,
 *    TL2-IP, and [Marathe et al. ICPP 2008].  We use commit time ordering to
 *    ensure that there are no delayed cleanup problems, we poll the timestamp
 *    variable to address doomed transactions, but unlike the above works, we
 *    use TinySTM-style extendable timestamps instead of TL2-style timestamps,
 *    which sacrifices some publication safety.
 */

#include "common/utils.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"
#include "../sandboxing.hpp"

using stm::TxThread;
using stm::timestamp;
using stm::timestamp_max;
using stm::last_complete;
using stm::orec_t;
using stm::get_orec;
using stm::WriteSet;
using stm::OrecList;
using stm::LockList;
using stm::WriteSetEntry;
using stm::id_version_t;
using stm::scope_t;
using stm::minimum;
using stm::maximum;

/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  struct OrecSandbox {
      static TM_FASTCALL bool  begin(TxThread*);
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
      static TM_FASTCALL void  write_ro(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void  write_rw(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void  commit_ro(TxThread*);
      static TM_FASTCALL void  commit_rw(TxThread*);

      static scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static bool validate(TxThread*);
  };
}

static bool
dirty(TxThread& tx) {
    ++tx.validations;
    return (tx.lazy_hashing_cursor != tx.r_orecs.m_size);
}

// This is super hacky and the moment. It violates all sorts of encapsulation
// design goals. A finished product would have a dedicated ReadLog class that
// handled this.
static bool
do_lazy_hashes(TxThread& tx) {
    OrecList& reads = tx.r_orecs;
    size_t& cursor = tx.lazy_hashing_cursor;

    // If we haven't read anything, then we don't need to do any work
    if (cursor == reads.m_size)
        return false;

    for (; cursor < reads.m_size; ++cursor)
        reads.m_elements[cursor] = get_orec(reads.m_elements[cursor]);

    return true;
}

/**
 *  OrecSandbox validate
 *
 *  [!] only call while not holding locks
 */
bool
OrecSandbox::validate(TxThread* tx)
{
    // Check tainted first, since it's purely local.
    if (!dirty(*tx))
        return true;

    // skip validation entirely if no one has committed
    if (tx->start_time == timestamp.val)
        return true;

    // We're ok for re-entrancy up to here, as long as we check the result from
    // do_lazy_hashes.
    stm::sandbox::InLib raii;

    // We're using lazy read log hashing. We need to go through and clean up
    // all of the addresses that we've logged-but-not-hashed. If we haven't
    // read anything new (the return value from do_lazy_hashes is false), then
    // we were consistent the last time we validated, and so we're still
    // consistent now (as if we were opaque).
    if (!do_lazy_hashes(*tx))
        return true;

    ++tx->full_validations;

    // We have read something since we were valid, and someone committed. Do a
    // full validation loop and scale start_time if we succeed. This is sort of
    // a consistent-snapshot validation thing, except that we deal with the
    // commit-fence window between timestamp and last_complete.
    uintptr_t newts = timestamp.val;

    // Fail validation if any of the orecs is locked or newer than my start
    // time.
    foreach (OrecList, i, tx->r_orecs) {
        if ((*i)->v.all > tx->start_time)
            return false;
    }

    // consistent snapshot is bracketed by last_complete, and we pick the
    // minimum to scale to
    uintptr_t cs = last_complete.val;
    tx->start_time = minimum(newts, cs);
    return true;
}

/**
 *  OrecSandbox begin:
 *
 *    We need a starting point for the transaction.  If an in-flight
 *    transaction is committed, but still doing writeback, we can either start
 *    at the point where that transaction had not yet committed, or else we can
 *    wait for it to finish writeback.  In this code, we choose the former
 *    option.
 */
bool
OrecSandbox::begin(TxThread* tx)
{
    tx->allocator.onTxBegin();

    // Start after the last cleanup, instead of after the last commit, to
    // avoid spinning in begin()
    tx->start_time = last_complete.val;
    tx->end_time = 0;
    return false;
}

/**
 *  OrecSandbox commit (read-only):
 *
 *    Read only sandboxed implementations need to succeed in validating their
 *    read set, or they have to abort.
 */
void
OrecSandbox::commit_ro(TxThread* tx)
{
    // stm::sandbox::InLib raii; validate blocks signals

    // have to validate because we might never have needed to --- this will
    // scale our timestamp unneccesarily... big deal
    if (!validate(tx))
        tx->tmabort(tx);

    // Standard read-only commit at this point.
    tx->r_orecs.reset();
    tx->lazy_hashing_cursor = 0;
    OnReadOnlyCommit(tx);
}

/**
 *  OrecSandbox commit (writing context):
 *
 *    OrecSandbox commit is like LLT: we get the locks, increment the
 *    counter, and then validate and do writeback.  As in other systems, some
 *    increments lead to skipping validation.
 *
 *    After writeback, we use a second, trailing counter to know when all
 *    txns who incremented the counter before this tx are done with
 *    writeback.  Only then can this txn mark its writeback complete.
 *
 *    When sandboxed there is a question about how we should acquire
 *    locks... should we validate first under the assumption that a sandboxed
 *    implementation is more likely to have aborted, or should we just go
 *    ahead and get the locks and validate like normal?
 *
 *      For now we just validate like normal, which avoids a bunch of work in
 *      read-mostly or single-threaded execution.
 */
void
OrecSandbox::commit_rw(TxThread* tx)
{
    stm::sandbox::InLib raii;           // don't handle validate signal

    // acquire locks
    foreach (WriteSet, i, tx->writes) {
        // get orec, read its version#
        orec_t* o = get_orec(i->addr);
        uintptr_t ivt = o->v.all;

        // if orec not locked, lock it and save old to orec
        if (ivt <= tx->start_time) {
            // abort if cannot acquire
            if (!bcasptr(&o->v.all, ivt, tx->my_lock.all))
                tx->tmabort(tx);
            // save old version to o->p, log lock
            o->p = ivt;
            tx->locks.insert(o);
        }
        // else if we don't hold the lock abort
        else if (ivt != tx->my_lock.all) {
            tx->tmabort(tx);
        }
    }

    // increment the global timestamp if we have writes
    tx->end_time = 1 + faiptr(&timestamp.val);

    // skip validation if possible
    if (tx->end_time != (tx->start_time + 1)) {
        // clean up any outstanding hashes we might have
        do_lazy_hashes(*tx);

        // inner loop that looks out for our locks, which is different than
        // normal validation
        foreach (OrecList, i, tx->r_orecs) {
            uintptr_t ivt = (*i)->v.all; // only read once
            if ((ivt > tx->start_time) && (ivt != tx->my_lock.all))
                tx->tmabort(tx);
        }
    }

    // run the redo log
    tx->writes.writeback();

    // release locks
    foreach (LockList, i, tx->locks) {
        (*i)->v.all = tx->end_time;
    }

    // now ensure that transactions depart from stm_end in the order that
    // they incremend the timestamp.  This avoids the "deferred update"
    // half of the privatization problem.
    while (last_complete.val != (tx->end_time - 1))
        spin64();
    last_complete.val = tx->end_time;

    // clean-up
    tx->r_orecs.reset();
    tx->lazy_hashing_cursor = 0;
    tx->writes.reset();
    tx->locks.reset();
    OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
}

/**
 *  OrecSandbox read (read-only transaction)
 *
 *    This is a traditional orec read for systems with extendable timestamps.
 *    However, we also poll the timestamp counter and validate any time a new
 *    transaction has committed, in order to catch doomed transactions.
 */
void*
OrecSandbox::read_ro(STM_READ_SIG(tx,addr,))
{
    // just log the address... we'll hash it during validation if we ever need
    // to
    tx->r_orecs.insert(reinterpret_cast<orec_t*>(addr));
    return *addr;
}

/**
 *  OrecSandbox read (writing transaction)
 *
 *    Identical to RO case, but with write-set lookup first
 */
void*
OrecSandbox::read_rw(STM_READ_SIG(tx,addr,mask))
{
    // check the log for a RAW hazard, we expect to miss
    WriteSetEntry log(STM_WRITE_SET_ENTRY(addr, NULL, mask));
    bool found = tx->writes.find(log);
    REDO_RAW_CHECK(found, log, mask);

    // reuse the ReadRO barrier, which is adequate here---reduces LOC
    void* val = read_ro(tx, addr STM_MASK(mask));
    REDO_RAW_CLEANUP(val, found, log, mask);
    return val;
}

/**
 *  OrecSandbox write (read-only context)
 *
 *    Simply buffer the write and switch to a writing context
 */
void
OrecSandbox::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
{
    tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
    OnFirstWrite(tx, read_rw, write_rw, commit_rw);
}

/**
 *  OrecSandbox write (writing context)
 *
 *    Simply buffer the write
 */
void
OrecSandbox::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
{
    tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
}

/**
 *  OrecSandbox unwinder:
 *
 *    This is a standard orec unwind function.  The only catch is that if a
 *    transaction aborted after incrementing the timestamp, it must wait its
 *    turn and then increment the trailing timestamp, to keep the two counters
 *    consistent.
 */
stm::scope_t*
OrecSandbox::rollback(STM_ROLLBACK_SIG(tx, except, len))
{
    PreRollback(tx);

    // Perform writes to the exception object if there were any... taking the
    // branch overhead without concern because we're not worried about
    // rollback overheads.
    STM_ROLLBACK(tx->writes, except, len);

    // release locks and restore version numbers
    foreach (LockList, i, tx->locks) {
        (*i)->v.all = (*i)->p;
    }
    tx->r_orecs.reset();
    tx->lazy_hashing_cursor = 0;
    tx->writes.reset();
    tx->locks.reset();

    // if we aborted after incrementing the timestamp, then we have to
    // participate in the global cleanup order to support our solution to
    // the deferred update half of the privatization problem.
    //
    // NB:  Note that end_time is always zero for restarts and retrys
    if (tx->end_time != 0) {
        while (last_complete.val < (tx->end_time - 1))
            spin64();
        last_complete.val = tx->end_time;
    }

    // we're going to longjmp from an abort---in_lib needs to be reset
    stm::sandbox::clear_in_lib();
    return PostRollback(tx, read_ro, write_ro, commit_ro);
}

/**
 *  OrecSandbox in-flight irrevocability: use abort-and-restart
 */
bool
OrecSandbox::irrevoc(TxThread* tx)
{
    return false;
}

/**
 *  Switch to OrecSandbox:
 *
 *  Install our signal handler.
 */
void
OrecSandbox::onSwitchTo()
{
    timestamp.val = maximum(timestamp.val, timestamp_max.val);
    last_complete.val = timestamp.val;
}

namespace stm {
  /**
   *  OrecSandbox initialization
   */
  template<>
  void initTM<OrecSandbox>()
  {
      // set the name
      stm::stms[OrecSandbox].name     = "OrecSandbox";

      // set the pointers
      stm::stms[OrecSandbox].begin    = ::OrecSandbox::begin;
      stm::stms[OrecSandbox].commit   = ::OrecSandbox::commit_ro;
      stm::stms[OrecSandbox].read     = ::OrecSandbox::read_ro;
      stm::stms[OrecSandbox].write    = ::OrecSandbox::write_ro;
      stm::stms[OrecSandbox].rollback = ::OrecSandbox::rollback;
      stm::stms[OrecSandbox].irrevoc  = ::OrecSandbox::irrevoc;
      stm::stms[OrecSandbox].switcher = ::OrecSandbox::onSwitchTo;
      stm::stms[OrecSandbox].validate = ::OrecSandbox::validate;
      stm::stms[OrecSandbox].privatization_safe = true;
      stm::stms[OrecSandbox].sandbox_signals = true;
  }
}
