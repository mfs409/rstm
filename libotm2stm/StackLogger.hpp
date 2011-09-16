/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef OTM2STM_STACKLOGGER_HPP__
#define OTM2STM_STACKLOGGER_HPP__

#include <string.h> // for memcpy
#include "stm/MiniVector.hpp"

namespace stm
{
  /**
   *  The StackLogger object is based on the Scope object from libitm2stm.
   *  The role of this object is to provide a simple means of logging values
   *  before they are modified.
   *
   *  The reason we need logging is that the compiler may encounter
   *  situations in which it cannot tell if a location is thread-local or
   *  not, and in other situations, in which case it must instrument the
   *  access.  Clearly, transactifying reads to the stack is a waste of
   *  cycles.  However, things get worse.  If a location is written
   *  transactionally, but is thread local, and a subsequent read does not
   *  use the TM, it won't see the right value.  Furthermore, if a value on
   *  the stack is written transactionally, we have problems at commit/abort
   *  time.  Either we can undo something that is in an invalid stack frame
   *  redo something that is in an invalid stack frame.  In both cases, it
   *  would be possible to trash the return address of the abort/commit
   *  function, or to overwrite locals of that function.
   *
   *  To get the right behavior, we do the following:
   *
   *   1 - when reading/writing, we check if the location is on the stack,
   *       and if so, we don't use the TM
   *
   *   2 - however, when writing, we first "undo log" the location, so that
   *       on abort we can restore the right value and ensure the transaction
   *       looks like it never happened.
   */
  class StackLogger
  {
      /**
       *  To handle arbitrary granularity, we use a special loggedword
       *  object.  This object tracks an address, a value, and a count
       *  of the number of bytes to actually write back
       *  be written back.
       */
      class LoggedWord
      {
          void** addr;          /// address that was logged
          void*  val;           /// value observed at that address
          size_t count;         /// count for the bytes that are valid

        public:
          /***  Simple costructor */
          LoggedWord(void** a, void* v, size_t b) : addr(a), val(v), count(b)
          { }

          /**
           *  Undo the write to this location.  It suffices to use memcpy to
           *  write back the bytes.  This isn't going to be fast, but it
           *  isn't on the critical path.
           */
          void undo() { memcpy(addr_, &val, count); }
      };

      /***  Convenience typedef */
      typedef stm::MiniVector<LoggedWord> UndoList;

      /*** List of locations that have been logged */
      UndoList   undolist;

      /**
       * templated helper methods to simplify the task of logging various
       * types.  Note that we don't care if a type is aligned when we log it,
       * because either the architecture supports unaligned access, or else
       * the type is aligned anyway.  Furthermore, undo simply uses memcpy,
       * so alignment isn't an issue then, either.
       *
       * NB: the compiler should unroll any loops, but we haven't verified
       */

      /**
       *  Basic helper: W is the number of words that comprise type T.  Log
       *  each word separately.
       */
      template <typename T, size_t W = sizeof(T) / sizeof(void*)>
      struct LOGGER
      {
          static void log(Scope* scope, T* addr)
          {
              void** address = reinterpret_cast<void**>(addr);
              for (size_t i = 0; i < W; ++i)
                  scope->log(address + i, *(address + i), sizeof(void*));
          }
      };

      /**
       *  Special case for when the type is smaller than a word.
       *
       *  [mfs] I don't trust this code right now.  On x86, it probably
       *        works.  However, on SPARC I think we're in trouble due to
       *        endianness
       */
      template <typename T>
      struct LOGGER<T, 0u>
      {
          static void log(Scope* const scope, const T* addr)
          {
              void** address = reinterpret_cast<void**>(const_cast<T*>(addr));
              union {
                  T val;
                  void* word;
              } cast = { *addr };
              scope->log(address, cast.word, sizeof(T));
         }
      };

      /**
       *  LOGGER uses this to actually update the undolist
       */
      void log(void** addr, void* value, size_t bytes)
      {
          undolist.insert(LoggedWord(addr, value, bytes));
      }

    public:

      /***  Constructor just creates an undo list with 16 entries */
      StackLogger() : undolist(16) { }

      /**
       *  When a transaction aborts or restarts, we call this to undo any
       *  out-of-tx-scope stack writes.
       *
       *  NB: if we don't come up with a prefilter to avoid logging the
       *      writes to in-tx-scope stack writes, we're going to need more
       *      logic in here to avoid overwriting the current frame.
       */
      void rollback()
      {
          for (UndoList::iterator i = undolist.end() - 1,
                   e = undolist.begin(); i >= e; --i)
              i->undo();
      }

      /***  To commit, we just drop the undo log */
      void commit() { undolist.reset(); }

      /**
       *  Public interface to LOGGER.  Calling log_for_undo() will result in a
       *  dispatch, through LOGGER, to log()
       */
      template <typename T>
      void log_for_undo(const T* address)
      {
          LOGGER<T>::log(this, address);
      }
  };
} // namespace stm

#endif // OTM2STM_STACKLOGGER_HPP__
