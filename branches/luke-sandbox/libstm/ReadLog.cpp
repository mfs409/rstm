#include "stm/ReadLog.hpp"
#include "algs/algs.hpp"    // get_orec

using stm::ReadLog;
using stm::OrecList;
using stm::get_orec;

ReadLog::ReadLog(const unsigned long capacity)
  : OrecList(capacity), cursor_(0)
{
}

void
ReadLog::reset()
{
    cursor_ = 0;
    OrecList::reset();
}

bool
ReadLog::doLazyHashes()
{
    if (cursor_ == m_size)
        return false;

    // cache-friendly back-to-front scan of the read log
    for (size_t i = m_size - 1; i >= cursor_; --i)
        m_elements[i] = get_orec(m_elements[i]);

    // update the cursor
    cursor_ = m_size;
    return true;
}
