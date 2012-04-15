/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_REDO_LOG_H
#define RSTM_REDO_LOG_H

#include <utility>

namespace stm {
  template <typename WordType,
            template <typename, typename> class Index,
            template <typename> class List>
  class RedoLog {
      class LogEntry {
        public:
          LogEntry() : address_(NULL), value_() {
          }

          LogEntry(void** adress, const WordType& value) : address_(adress),
                                                           value_(value) {
          }

          void** address() const {
              return address_;
          }

          const WordType& value() const {
              return value_;
          }

          void merge(const WordType& rhs) {
              value_.merge(rhs);
          }

          void redo() const {
              value_.writeTo(address_);
          }

        private:
          void** address_;
          WordType value_;
      };

      typedef Index<void**, int> IndexType;
      typedef List<LogEntry> ListType;

    public:
      typedef WordType Word;
      typedef typename ListType::iterator iterator;
      typedef typename ListType::const_iterator const_iterator;

      RedoLog() : index_(), log_() {
      }

      RedoLog(int init) : index_(), log_() {
          log_.reserve(init);
      }

      int size() const {
          return log_.size();
      }

      void clear() {
          index_.clear();
          log_.clear();
      }

      const WordType* const find(void** addr) const {
          typename IndexType::const_iterator i = index_.find(addr);
          return (i != index_.end() && log_[i->second].address() == addr) ?
              &log_[i->second].value() : NULL;
      }

      void insert(void** addr, const WordType& value) {
          typename IndexType::iterator i = index_.find(addr);
          if (i == index_.end() || log_[i->second].address() != addr) {
              log_.push_back(LogEntry(addr, value));
              index_[addr] = log_.size() - 1;
          }
          else {
              log_[i->second].merge(value);
          }
      }

      void redo() const {
          for (typename ListType::const_iterator i = log_.begin(),
                                                 e = log_.end(); i != e; ++i) {
              i->redo();
          }
      }

      iterator begin() {
          return log_.begin();
      }

      iterator end() {
          return log_.end();
      }

      const_iterator begin() const {
          return log_.begin();
      }

      const_iterator end() const {
          return log_.end();
      }

    private:
      IndexType index_;
      ListType log_;
  };

  class Word {
    public:
      Word() : value_(NULL) {
      }

      Word(void* value, uintptr_t) : value_(value) {
      }

      void* value() const {
          return value_;
      }

      uintptr_t mask() const {
          return ~0;
      }

      void merge(const Word& rhs) {
          value_ = rhs.value_;
      }

      void writeTo(void** address) const {
          *address = value_;
      }

    private:
      void* value_;
  };

  class MaskedWord {
    public:
      MaskedWord() : value_(NULL), mask_(0) {
      }

      MaskedWord(void* value, uintptr_t mask) : value_(value), mask_(mask) {
      }

      void* value() const {
          return value_;
      }

      uintptr_t mask() const {
          return mask_;
      }

      void merge(const MaskedWord& rhs) {
          value_ = (void*)(((uintptr_t)value_ & ~rhs.mask_) |
                           ((uintptr_t)rhs.value_ & rhs.mask_));
          mask_ = mask_ | rhs.mask_;
      }

      void writeTo(void** address) const {
          if (mask_ == ~0) {
              *address = value_;
              return;
          }
      }

    private:
      void* value_;
      uintptr_t mask_;
  };
}

#endif // RSTM_REDO_LOG_H
