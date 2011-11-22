/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "stm/WBMMPolicy.hpp"
#include "stm/macros.hpp"
#include "sandboxing/sandboxing.hpp"
using namespace stm;

namespace
{
/*** figure out if one timestamp is strictly dominated by another */
static inline bool
is_strictly_older(uint32_t* newer, uint32_t* older, uint32_t old_len)
{
    for (uint32_t i = 0; i < old_len; ++i)
        if ((newer[i] <= older[i]) && (newer[i] & 1))
            return false;
    return true;
}
}

pad_word_t stm::trans_nums[MAX_THREADS] = {{0}};

/*** Node type for a list of timestamped void*s */
struct stm::limbo_t
{
    /*** Number of void*s held in a limbo_t */
    static const uint32_t POOL_SIZE = 32;

    /*** Set of void*s */
    void*     pool[POOL_SIZE];

    /*** Timestamp when last void* was added */
    uint32_t  ts[MAX_THREADS];

    /*** # valid timestamps in ts, or # elements in pool */
    uint32_t  length;

    /*** NehelperMin pointer for the limbo list */
    limbo_t*  older;

    /*** The constructor for the limbo_t just zeroes out everything */
    limbo_t() : length(0), older(NULL) {
    }
};

/**
 *  Constructing the DeferredReclamationMMPolicy is very easy Null out the
 *  timestamp for a particular thread.  We only call this at initialization.
 */
WBMMPolicy::WBMMPolicy()
    : prelimbo(new limbo_t()),
      limbo(NULL),
      frees(128),
      allocs(128)
{
}

/**
 *  Wrapper to thread-specific allocator for allocating memory
 */
void*
WBMMPolicy::txAlloc(size_t const &size)
{
    stm::sandbox::InLib block;

    void* ptr = malloc(size);
    if (*my_ts % 2)
        allocs.insert(ptr);
    return ptr;
}

/**
 *  Wrapper to thread-specific allocator for freeing memory
 */
void
WBMMPolicy::txFree(void* ptr)
{
    stm::sandbox::InLib block;

    if (*my_ts % 2)
        frees.insert(ptr);
    else
        free(ptr);
}

/**
 *  Since a TxThread constructs its allocator before it gets its id, we need
 *  the TxThread to inform the allocator of its id from within the constructor,
 *  via this method.
 */
void
WBMMPolicy::setID(uint32_t id)
{
    my_ts = &trans_nums[id].val;
}

/*** On abort, unroll allocs, clear lists, exit epoch */
void
WBMMPolicy::onTxAbort()
{
    foreach (AddressList, i, allocs) {
        free(*i);
    }
    frees.reset();
    allocs.reset();
    *my_ts = 1+*my_ts;
}

/**
 *  Schedule a pointer for reclamation.  Reclamation will not happen
 *  until enough time has passed.
 */
void
WBMMPolicy::schedForReclaim(void* ptr)
{
    // insert /ptr/ into the prelimbo pool and increment the pool size
    prelimbo->pool[prelimbo->length++] = ptr;
    // if prelimbo is not full, we're done
    if (prelimbo->length != prelimbo->POOL_SIZE)
        return;
    // if prelimbo is full, we have a lot more work to do
    handleFullPrelimbo();
}


void
WBMMPolicy::onTxCommit()
{
    foreach (AddressList, i, frees) {
        schedForReclaim(*i);
    }
    frees.reset();
    allocs.reset();
    *my_ts = 1+*my_ts;
}


// [mfs] the caller has an odd timestamp at the time of the call.  Does that
//       mean it will not reclaim some things as early as it might otherwise?
void
WBMMPolicy::handleFullPrelimbo()
{
    // get the current timestamp from the epoch
    prelimbo->length = threadcount.val;
    for (uint32_t i = 0, e = prelimbo->length; i < e; ++i)
        prelimbo->ts[i] = trans_nums[i].val;

    // push prelimbo onto the front of the limbo list:
    prelimbo->older = limbo;
    limbo = prelimbo;

    //  check if anything after limbo->head is dominated by ts.  Exit the loop
    //  when the list is empty, or when we find something that is strictly
    //  dominated.
    //
    //  NB: the list is in sorted order by timestamp.
    limbo_t* current = limbo->older;
    limbo_t* prev = limbo;
    while (current != NULL) {
        if (is_strictly_older(limbo->ts, current->ts, current->length))
            break;
        prev = current;
        current = current->older;
    }

    // If current != NULL, it is the head of a list of reclaimables
    if (current) {
        // detach /current/ from the list
        prev->older = NULL;

        // free all blocks in each node's pool and free the node
        while (current != NULL) {
            // free blocks in current's pool
            for (unsigned long i = 0; i < current->POOL_SIZE; i++)
                free(current->pool[i]);

            // free the node and move on
            limbo_t* old = current;
            current = current->older;
            delete old;
        }
    }
    prelimbo = new limbo_t();
}
