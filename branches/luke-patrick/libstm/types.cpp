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
 *  In the types/ folder, we have a lot of data structure implementations.  In
 *  some cases, the optimal implementation will have a 'noinline' function that
 *  is rarely called.  To actually ensure that the 'noinline' behavior is
 *  achieved, we put the implementations of those functions here, in a separate
 *  compilation unit.
 */

#include "stm/lib_globals.hpp"
#include "stm/metadata.hpp"
#include "stm/MiniVector.hpp"
#include "stm/WriteSet.hpp"
#include "stm/UndoLog.hpp"
#include "stm/ValueList.hpp"
#include "policies/policies.hpp"
#include "sandboxing.hpp"

namespace
{
  /**
   * We use malloc a couple of times here, and this makes it a bit easier
   */
  template <typename T>
  inline T* typed_malloc(size_t N)
  {
      return static_cast<T*>(malloc(sizeof(T) * N));
  }
}

namespace stm
{

#if !defined(STM_ABORT_ON_THROW)
  void UndoLog::undo()
  {
      for (iterator i = end() - 1, e = begin(); i >= e; --i)
          i->undo();
  }
#else
  void UndoLog::undo(void** exception, size_t len)
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
  bool ByteLoggingUndoLogEntry::filterSlow(void** lower, void** upper)
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
} // namespace stm
