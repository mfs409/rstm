/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include "stm/ReadLog.hpp"
#include "stm/txthread.hpp" // expand() validation
#include "common/utils.hpp" // stm::Guard
#include "algs/algs.hpp"    // get_orec

using stm::ReadLog;
using stm::OrecList;
using stm::get_orec;
using stm::TxThread;
using stm::Self;
using stm::UNRECOVERABLE;
using stm::Guard;
using std::sig_atomic_t;

ReadLog::ReadLog(const unsigned long capacity)
    : OrecList(capacity), cursor_(0), hashing_(0)
{
}

void
ReadLog::reset()
{
    cursor_ = 0;
    OrecList::reset();
}

/// Called by sandboxing STM algorithms in order to hash the set of addresses
/// that we may have logged. The read-set stores pointers, so we don't need
/// separate storage for this operation. Just scan through the [cursor_,
/// m_size) sequence of addresses and replace them by their hashes.
///
/// Return true if we hashed anything, otherwise return false. The caller uses
/// this to filter out validation.
///
/// [!] This is *not* reentrant.
bool
ReadLog::doLazyHashes()
{
    Guard no_reentry(hashing_);

    if (cursor_ == m_size)
        return false;

    for (; cursor_ < m_size; ++cursor_)
        m_elements[cursor_] = get_orec(m_elements[cursor_]);

    return true;

    // // cache-friendly back-to-front scan of the read log
    // for (size_t i = m_size - 1; i >= cursor_; --i)
    //     m_elements[i] = get_orec(m_elements[i]);
    //
    // update the cursor
    // cursor_ = m_size;
    //
    // [ld] this didn't work for some reason
}

/// Read log expansion gives us a nice place to proactively catch infinite
/// loops due to sandboxing inconsistency. When we're asked to expand it makes
/// sense to make sure that we're valid before doing so. The log isn't giong to
/// string,
void
ReadLog::expand()
{
    if (!TxThread::tmvalidate(Self))
        TxThread::tmabort(Self);
    OrecList::expand();
}
