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
 *  Implement an undo log so that we can centralize all logic for undo behavior
 *  in in-place update STMs
 */

#ifndef RSTM_UNDO_LOG_H
#define RSTM_UNDO_LOG_H

#include "MiniVector.hpp"

/**
 *  An undo log is a pretty simple structure. We never need to search it, so
 *  its only purpose is to store stuff and write stuff out when we abort. It is
 *  split out into its own class in order to deal with the configuration-based
 *  behavior that we need it to observe, like byte-accesses, abort-on-throw,
 *  etc.
 */
namespace stm
{
  /**
   *  The undo log entry is the type stored in the undo log. If we're
   *  byte-logging then it has a mask, otherwise it's just an address-value
   *  pair.
   */
  template <typename WordType>
  class GenericUndoLog
  {
      struct ListEntry {
          void** address;
          WordType value;

          ListEntry(void** a, const WordType& v) : address(a), value(v) {
          }
      };

      typedef MiniVector<ListEntry> ListType;
      ListType list_;

    public:
      GenericUndoLog(const uintptr_t cap) : list_(cap) {
      }

      ~GenericUndoLog() {
      }

      void reset() {
          list_.reset();
      }

      void insert(void** addr, void* val, uintptr_t mask) {
          list_.insert(ListEntry(addr, WordType(val, mask)));
      }

      void undo() const {
          for (typename ListType::const_iterator i = list_.begin(),
                                                 e = list_.end(); i != e; ++i)
              i->value.writeTo(i->address);
      }
  };
}
#endif // RSTM_UNDO_LOG_H
