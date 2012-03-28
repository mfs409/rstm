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
 *  NanoSandbox Implementation:
 *
 *    This STM is a surprising step backwards from the sorts of algorithms we
 *    are used to.  It accepts quadratic validation overhead, and eschews any
 *    timestamps.  It also has a limited set of Orecs.
 *
 *    The justification for this STM is two-fold.  First, it should not fare
 *    badly on multi-chip machines, since it lacks any bottlenecks.  Second, it
 *    should not fare badly on small transactions, despite the quadratic
 *    overhead.
 */

#include "../profiling.hpp"
#include "algs.hpp"
#include "RedoRAWUtils.hpp"

using stm::UNRECOVERABLE;
using stm::TxThread;
using stm::WriteSet;
using stm::WriteSetEntry;
using stm::OrecList;
using stm::LockList;
using stm::orec_t;
using stm::NanorecList;
using stm::nanorec_t;
using stm::get_nanorec;
using stm::id_version_t;


/**
 *  Declare the functions that we're going to implement, so that we can avoid
 *  circular dependencies.
 */
namespace {
  struct NanoSandbox
  {
      static TM_FASTCALL bool begin(TxThread*);
      static TM_FASTCALL void* read_ro(STM_READ_SIG(,,));
      static TM_FASTCALL void* read_rw(STM_READ_SIG(,,));
      static TM_FASTCALL void write_ro(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void write_rw(STM_WRITE_SIG(,,,));
      static TM_FASTCALL void commit_ro(TxThread*);
      static TM_FASTCALL void commit_rw(TxThread*);

      static stm::scope_t* rollback(STM_ROLLBACK_SIG(,,));
      static bool irrevoc(TxThread*);
      static void onSwitchTo();
      static bool validate(TxThread&);
  };

  bool
  NanoSandbox::validate(TxThread& tx) {
  }

  /**
   *  NanoSandbox begin:
   */
  bool
  NanoSandbox::begin(TxThread* tx)
  {
      tx->allocator.onTxBegin();
      return false;
  }

  /**
   *  NanoSandbox commit (read-only context)
   */
  void
  NanoSandbox::commit_ro(TxThread* tx)
  {
      // read-only, so reset the orec list and we are done
      tx->nanorecs.reset();
      OnReadOnlyCommit(tx);
  }

  /**
   *  NanoSandbox commit (writing context)
   *
   *    There are no optimization opportunities here... we grab all locks,
   *    then validate, then do writeback.
   */
  void
  NanoSandbox::commit_rw(TxThread* tx)
  {
      // acquire locks
      foreach (WriteSet, i, tx->writes) {
          // get orec, read its version#
          orec_t* o = get_nanorec(i->addr);
          id_version_t ivt;
          ivt.all = o->v.all;

          // if unlocked and we can lock it, do so
          if (ivt.all != tx->my_lock.all) {
              if (!ivt.fields.lock) {
                  if (!bcasptr(&o->v.all, ivt.all, tx->my_lock.all))
                      tx->tmabort(tx);
                  // save old version to o->p, remember that we hold the lock
                  o->p = ivt.all;
                  tx->locks.insert(o);
              }
              else {
                  tx->tmabort(tx);
              }
          }
      }

      // validate (variant for when locks are held)
      foreach (NanorecList, i, tx->nanorecs) {
          uintptr_t ivt = i->o->v.all;
          // if orec does not match val, then it must be locked by me, with its
          // old val equalling my expected val
          if ((ivt != i->v) && ((ivt != tx->my_lock.all) || (i->v != i->o->p)))
              tx->tmabort(tx);
      }

      // run the redo log
      tx->writes.writeback();

      // release locks
      foreach (LockList, i, tx->locks)
          (*i)->v.all = (*i)->p+1;

      // clean-up
      tx->nanorecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      OnReadWriteCommit(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  NanoSandbox read (read-only context):
   */
  void*
  NanoSandbox::read_ro(STM_READ_SIG(tx,addr,))
  {
      // get the orec addr
      orec_t* o = get_nanorec(addr);

      while (true) {
          // read orec
          id_version_t ivt;
          ivt.all = o->v.all;
          CFENCE;

          // read the location
          void* tmp = *addr;
          CFENCE;

          // re-read orec
          uintptr_t ivt2 = o->v.all;

          // common case: valid read
          if ((ivt.all == ivt2) && (!ivt.fields.lock)) {
              // log the read
              tx->nanorecs.insert(nanorec_t(o, ivt2));
              // validate the whole read set, then return the value we just read
              foreach (NanorecList, i, tx->nanorecs)
                  if (i->o->v.all != i->v)
                      tx->tmabort(tx);
              return tmp;
          }

          // if lock held, spin before retrying
          if (o->v.fields.lock)
              spin64();
      }
  }

  /**
   *  NanoSandbox read (writing context):
   */
  void*
  NanoSandbox::read_rw(STM_READ_SIG(tx,addr,mask))
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
   *  NanoSandbox write (read-only context):
   */
  void
  NanoSandbox::write_ro(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
      OnFirstWrite(tx, read_rw, write_rw, commit_rw);
  }

  /**
   *  NanoSandbox write (writing context):
   */
  void
  NanoSandbox::write_rw(STM_WRITE_SIG(tx,addr,val,mask))
  {
      // add to redo log
      tx->writes.insert(WriteSetEntry(STM_WRITE_SET_ENTRY(addr, val, mask)));
  }

  /**
   *  NanoSandbox unwinder:
   *
   *    Release any locks we acquired (if we aborted during a commit()
   *    operation), and then reset local lists.
   */
  stm::scope_t*
  NanoSandbox::rollback(STM_ROLLBACK_SIG(tx, except, len))
  {
      PreRollback(tx);

      // Perform writes to the exception object if there were any... taking the
      // branch overhead without concern because we're not worried about
      // rollback overheads.
      STM_ROLLBACK(tx->writes, except, len);

      // release the locks and restore version numbers
      foreach (LockList, i, tx->locks)
          (*i)->v.all = (*i)->p;

      // undo memory operations, reset lists
      tx->nanorecs.reset();
      tx->writes.reset();
      tx->locks.reset();
      return PostRollback(tx, read_ro, write_ro, commit_ro);
  }

  /**
   *  NanoSandbox in-flight irrevocability:
   */
  bool NanoSandbox::irrevoc(TxThread*) {
      return false;
  }

  /**
   *  Switch to NanoSandbox:
   *
   *    Since NanoSandbox does not use timestamps, it can't use the regular orecs, or
   *    else switching would get nasty... that means that we don't need to do
   *    anything here.
   */
  void NanoSandbox::onSwitchTo() {
  }
}

namespace stm {
  /**
   *  NanoSandbox initialization
   */
  template<>
  void initTM<NanoSandbox>()
  {
      // set the name
      stms[NanoSandbox].name      = "NanoSandbox";

      // set the pointers
      stms[NanoSandbox].begin     = ::NanoSandbox::begin;
      stms[NanoSandbox].commit    = ::NanoSandbox::commit_ro;
      stms[NanoSandbox].read      = ::NanoSandbox::read_ro;
      stms[NanoSandbox].write     = ::NanoSandbox::write_ro;
      stms[NanoSandbox].rollback  = ::NanoSandbox::rollback;
      stms[NanoSandbox].irrevoc   = ::NanoSandbox::irrevoc;
      stms[NanoSandbox].switcher  = ::NanoSandbox::onSwitchTo;
      stms[NanoSandbox].privatization_safe = false;
      stms[NanoSandbox].sandbox_signals = true;
  }
}
