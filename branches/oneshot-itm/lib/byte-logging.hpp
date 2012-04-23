/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_BYTE_LOGGING_H
#define RSTM_BYTE_LOGGING_H

#include "WriteSet.hpp"
#include "ValueList.hpp"
#include "UndoLog.hpp"

/**
 *  This file contains everything that you always wanted to know about byte
 *  logging in rstm.
 */
namespace stm {

  /** We use this type for metaprogramming. */
  class NullType {};

  class Word {
    public:
      Word() : value_(NULL) {
      }

      Word(void* value, uintptr_t) : value_(value) {
      }

      void* value() const {
          return value_;
      }

      void setValue(void* value) {
          value_ = value;
      }

      uintptr_t mask() const {
          return ~0;
      }

      void setMask(uintptr_t) {
      }

      void merge(const Word& rhs) {
          value_ = rhs.value_;
      }

      static void Write(void** addr, void* val, uintptr_t) {
          *addr = val;
      }

      void writeTo(void** address) const {
          Write(address, value_, ~0);
      }

      bool equals(void* value) const {
          return (value_ == value);
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
          // http://graphics.stanford.edu/~seander/bithacks.html#MaskedMerge
          uintptr_t v = (uintptr_t)value_;
          value_ = (void*)(v ^ ((v ^ (uintptr_t)rhs.value_) & rhs.mask_));
          mask_ = mask_ | rhs.mask_;
      }

      static void Write(void** addr, void* val, uintptr_t mask) {
          if (mask == ~0) {
              *addr = val;
              return;
          }

          union {
              void** word;
              uint8_t* bytes;
          } uaddr = { addr };

          union {
              void* word;
              uint8_t bytes[sizeof(void*)];
          } uval = { val };

          union {
              uintptr_t word;
              uint8_t bytes[sizeof(uintptr_t)];
          } umask = { mask };

          // We're just going to write out individual bytes, which turns all
          // subword accesses into byte accesses. This might be inefficient but
          // should be correct, since the locations that we're writing to are
          // supposed to be locked, and if there's a data race we can have any
          // sort of behavior.
          for (unsigned i = 0, e = sizeof(void*); i < e; ++i)
              if (umask.bytes[i] == 0xFF)
                  uaddr.bytes[i] = uval.bytes[i];
      }

      void writeTo(void** address) const {
          Write(address, value_, mask_);
      }

      bool equals(void* val) const {
          return (((uintptr_t)value_ & mask_) == ((uintptr_t)val & mask_));
      }

    private:
      void* value_;
      uintptr_t mask_;
  };

#if defined(STM_WS_WORDLOG)
  typedef Word LoggingWordType;
#elif defined(STM_WS_BYTELOG)
  typedef MaskedWord LoggingWordType;
#else
#error WriteSet logging granularity configuration error.
#endif
  typedef GenericWriteSet<LoggingWordType> WriteSet;
  typedef GenericValueList<LoggingWordType> ValueList;
  typedef GenericUndoLog<LoggingWordType> UndoLog;
}

#endif // RSTM_BYTE_LOGGING_H
