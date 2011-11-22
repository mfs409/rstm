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
 *  In order to get allocation and deallocation to work correctly inside of a
 *  speculative transactional region, we need to be sure that a doomed
 *  transaction cannot access memory that has been returned to the OS.
 *
 *  WBMMPolicy is RSTM's variant of epoch-based reclamation.  It is similar
 *  to proposals by [Fraser PhD 2003] and [Hudson ISMM 2006].
 *
 *  Note that this file has real code in it, and that code gets inlined into
 *  many places.  It's not pretty, and we may eventually want to reduce the
 *  footprint of this file on the rest of the project.
 */

#ifndef WBMMPOLICY_HPP__
#define WBMMPOLICY_HPP__

#include <stdlib.h>
#include <stm/config.h>
#include "stm/MiniVector.hpp"
#include "stm/metadata.hpp"

namespace stm
{
/*** forward declare the threadcount used by TxThread */
extern pad_word_t threadcount;

/*** store every thread's counter */
extern pad_word_t trans_nums[MAX_THREADS];

/*** Node type for a list of timestamped void*s */
struct limbo_t;

/**
 * WBMMPolicy
 *  - log allocs and frees from within a transaction
 *  - on abort, free any allocs
 *  - on commit, replay any frees
 *  - use epochs to prevent reclamation during a doomed transaction's
 *    execution
 */
class WBMMPolicy
{
    /*** location of my timestamp value */
    volatile uintptr_t* my_ts;

    /*** As we mark things for deletion, we accumulate them here */
    limbo_t* prelimbo;

    /*** sorted list of timestamped reclaimables */
    limbo_t* limbo;

    /*** List of objects to delete if the current transaction commits */
    AddressList frees;

    /*** List of objects to delete if the current transaction aborts */
    AddressList allocs;

    /**
     *  Schedule a pointer for reclamation.  Reclamation will not happen
     *  until enough time has passed.
     */
    void schedForReclaim(void* ptr);

    /**
     *  This code is the cornerstone of the WBMMPolicy.  We buffer lots of
     *  frees onto a prelimbo list, and then, at some point, we must give
     *  that list a timestamp and tuck it away until the timestamp expires.
     *  This is how we do it.
     */
    void handleFullPrelimbo();

  public:

    WBMMPolicy();

    /**
     *  Since a TxThread constructs its allocator before it gets its id, we
     *  need the TxThread to inform the allocator of its id from within the
     *  constructor, via this method.
     */
    void setID(uint32_t id);

    /*** Wrapper to thread-specific allocator for allocating memory */
    void* txAlloc(size_t const &size);

    /*** Wrapper to thread-specific allocator for freeing memory */
    void txFree(void* ptr);

    /*** On begin, move to an odd epoch and start logging */
    void onTxBegin() { *my_ts = 1 + *my_ts; }

    /*** On abort, unroll allocs, clear lists, exit epoch */
    void onTxAbort();

    /*** On commit, perform frees, clear lists, exit epoch */
    void onTxCommit();

}; // class stm::WBMMPolicy
} // namespace stm

#endif // WBMMPOLICY_HPP__
