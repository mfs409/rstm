/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "stm/UndoLog.hpp"

using stm::UndoLog;
using stm::ByteLoggingUndoLogEntry;

#if !defined(STM_ABORT_ON_THROW)
void
UndoLog::undo()
{
    for (iterator i = end() - 1, e = begin(); i >= e; --i)
        i->undo();
}
#else
void
UndoLog::undo(void** exception, size_t len)
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
bool
ByteLoggingUndoLogEntry::filterSlow(void** lower, void** upper)
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

