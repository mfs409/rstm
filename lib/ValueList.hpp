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

#include <utility>
#include <algorithm>
#include "MiniVector.hpp"

namespace stm {
  /**
   *  We use the ValueList class to log address/value pairs for our
   *  value-based-validation implementations---NOrec and NOrecPrio currently.
   */
  template <typename WordType>
  class GenericValueList {

      typedef std::pair<void**, WordType> ListEntry;
      typedef MiniVector<ListEntry> ListType;
      ListType list_;

      /**
       *  Simple reduction functor or std::for_each, keeps track of if the
       *  validation succeeded for each entry.
       */
      struct Validate {
          bool valid;

          Validate() : valid(true) {
          }

          void operator()(const ListEntry& i) {
              valid &= i.second.equals(*i.first);
          }
      };

      bool __attribute__((noinline)) validateSlow() const {
          return std::for_each(list_.begin(), list_.end(), Validate()).valid;
      }

    public:
      GenericValueList(const unsigned long cap) : list_(cap) {
      }

      ~GenericValueList() {
      }

      void reset() {
          list_.reset();
      }

      void insert(void** addr, void* val, uintptr_t mask) {
          list_.insert(std::make_pair(addr, WordType(val, mask)));
      }

      bool validate() const {
          // don't branch in the loop---consider it backoff if we fail
          // validation early
          //
          // TODO: we've never checked to see if this "backoff" strategy makes
          //       any difference, or if validating back to front makes any
          //       sense
          return (list_.size()) ? validateSlow() : true;
      }
  };
}

#endif // STM_VALUE_LIST_HPP
