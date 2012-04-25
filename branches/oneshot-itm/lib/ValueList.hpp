/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef STM_VALUE_LIST_HPP
#define STM_VALUE_LIST_HPP

#include "MiniVector.hpp"

namespace stm {
  /**
   *  We use the ValueList class to log address/value pairs for our
   *  value-based-validation implementations---NOrec and NOrecPrio currently.
   */
  template <typename WordType>
  class GenericValueList {
      struct ListEntry {
          void** address;
          WordType value;

          ListEntry(void** a, const WordType& v) : address(a), value(v) {
          }
      };

      typedef MiniVector<ListEntry> ListType;
      ListType list_;

    public:
      GenericValueList(const unsigned long cap) : list_(cap) {
      }

      ~GenericValueList() {
      }

      void reset() {
          list_.reset();
      }

      void insert(void** addr, void* val, uintptr_t mask) {
          list_.insert(ListEntry(addr, WordType(val, mask)));
      }

      bool validate() const {
          // don't branch in the loop---consider it backoff if we fail
          // validation early
          //
          // TODO: we've never checked to see if this "backoff" strategy makes
          //       any difference, or if validating back to front makes any
          //       sense
          bool valid = true;
          for (typename ListType::const_iterator i = list_.begin(),
                                                 e = list_.end(); i != e; ++i)
              valid &= i->value.equals(*i->address);
          return valid;
      }
  };
}

#endif // STM_VALUE_LIST_HPP
