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
#include "WriteSet.hpp"
#include "ValueList.hpp"
#include "UndoLog.hpp"

namespace stm
{
  typedef MiniVector<orec_t*> OrecList;     // vector of orecs

  /**
   *  Store per-thread metadata.  There isn't much for CGL...
   */
  struct TX
  {
      // 25 words + mm (9) + writeset (9) = 43 words = 172 bytes = 3 cache
      // lines :(
      //
      // NB: we could save a fair bit by using these more carefully (e.g.,
      // order and end_time are never both used) or by flattening things more
      // aggressively (esp minivectors, which are 3 words each) Allocator
      // space overhead is also worrysome

      /*** for flat nesting ***/
      int nesting_depth;

      /*** for rollback */
      scope_t* scope;

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
      TX()
          : nesting_depth(0), commits_ro(0),
            commits_rw(0), aborts(0), scope(NULL), allocator(),
            start_time(0), writes(64), locks(64), vlist(64),
            r_orecs(64), ts_cache(0), order(-1), turbo(false),
            end_time(0), undo_log(64)
      {
          id = faiptr(&threadcount.val);
          threads[id] = this;
          // set up my lock word
          my_lock.fields.lock = 1;
          my_lock.fields.id = id;
          // NB: unused by CGL
          allocator.setID(id);
      }
  };

  /**
   *  Need to forward-declare the fact of the tm_abort function, since
   *  virtually every tm implementation will use it to abort
   */
  NORETURN
  void tm_abort(TX* tx);
}

#endif // TX_HPP__
