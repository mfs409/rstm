/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef TX_HPP__
#define TX_HPP__

#include <stdint.h>
#include "metadata.hpp"
#include "MiniVector.hpp"
#include "WBMMPolicy.hpp"
#include "UndoLog.hpp"
#include "UserCallbackLog.hpp"
#include "checkpoint.hpp"
#include "byte-logging.hpp"

namespace stm {
  typedef MiniVector<orec_t*> OrecList;     // vector of orecs

  /**
   *  Store per-thread metadata.  There isn't much for CGL...
   */
  struct TX {
      // 24 words + checkpoint + mm (9) + writeset (9) = 43 words = 172 bytes = 3 cache
      // lines :(
      //
      // NB: we could save a fair bit by using these more carefully (e.g.,
      // order and end_time are never both used) or by flattening things more
      // aggressively (esp minivectors, which are 3 words each) Allocator
      // space overhead is also worrysome

      /*** for flat nesting ***/
      int nesting_depth;

      /*** for rollback (flat nesting) */
      checkpoint_t checkpoint;

      /*** unique id for this thread ***/
      int id;

      uintptr_t      ts_cache;      // last validation time
      intptr_t       order;         // for stms that order txns eagerly
      uintptr_t      start_time;    // start time of transaction
      id_version_t   my_lock;       // lock word for orec STMs
      OrecList       locks;         // list of all locks held by tx
      OrecList       r_orecs;       // read set for orec STMs
      WriteSet       writes;        // write set
      ValueList      vlist;         // NOrec read log
      uintptr_t      end_time;

      UndoLog        undo_log;      // etee undo log

      bool turbo; // tml haslock or ordered txn in turbo mode

      UserCallbackLog userCallbacks;    //
      uint32_t cxa_catch_count;         // gcctm exception handling
      void *cxa_unthrown;               // "

      /*** number of RO commits ***/
      int commits_ro;

      /*** number of RW commits ***/
      int commits_rw;

      /*** number of aborts ***/
      int aborts;

      /*** for allocation ***/
      WBMMPolicy     allocator;     // buffer malloc/free

      /*** CM STUFF ***/
      uint32_t       consec_aborts; // count consec aborts
      uint32_t       seed;          // for randomized backoff
      volatile uint32_t alive;      // for STMs that allow remote abort
      bool           strong_HG;     // for strong hourglass

      /*** constructor ***/
      TX() : nesting_depth(0),
             checkpoint(),
             id(),
             ts_cache(0),
             order(-1),
             start_time(0),
             my_lock(),
             locks(64),
             r_orecs(64),
             writes(64),
             vlist(64),
             end_time(0),
             undo_log(64),
             turbo(false),
             userCallbacks(),
             cxa_catch_count(0),
             cxa_unthrown(0),
             commits_ro(0),
             commits_rw(0),
             aborts(0),
             allocator(),
             consec_aborts(0),
             seed(0),
             alive(0),
             strong_HG(false) {
          id = faiptr(&threadcount.val);
          threads[id] = this;
          // set up my lock word
          my_lock.fields.lock = 1;
          my_lock.fields.id = id;
          // NB: unused by CGL
          allocator.setID(id);
      }

    private:
      TX(const TX&);
      TX& operator=(const TX&);
  };

  /** We need access to the satck pointer to do filtering. */
  static inline void* get_stack_pointer_from_checkpoint(const TX* const tx) {
      return tx->checkpoint[CHECKPOINT_SP_OFFSET];
  }
}

#endif // TX_HPP__
