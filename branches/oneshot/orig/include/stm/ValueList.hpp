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

/**
 *  We use the ValueList class to log address/value pairs for our
 *  value-based-validation implementations---NOrec and NOrecPrio currently. We
 *  generally log things at word granularity, and during validation we check to
 *  see if any of the bits in the word has changed since the word was originally
 *  read. If they have, then we have a conflict.
 *
 *  This word-granularity continues to be correct when we have enabled byte
 *  logging (because we're building for C++ TM compatibility), but it introduces
 *  the possibility of byte-level false conflicts. One of VBV's advantages is
 *  that there are no false conflicts. In order to preserve this behavior, we
 *  offer the user the option to use the byte-mask (which is already enabled for
 *  byte logging) to do byte-granularity validation. The disadvantage to this
 *  technique is that the read log entry size is increased by the size of the
 *  stored mask (we could optimize for 64-bit Linux and pack the mask into an
 *  unused part of the logged address, but we don't yet have this capability).
 *
 *  We also must be aware of the potential for both instrumented and
 *  uninstrumented accesses to the "same" stack location (i.e., same stack
 *  address). It isn't correct to fail validation if the reason was our own
 *  in-place write. We use the TxThread's stack_high and stack_low (which is
 *  the low water mark maintained in NOrec's read barrier) addresses to filter
 *  validation.
 *
 *  This file implements the value log given the current configuration settings
 *  in stm/config.h.
 */
#include "stm/config.h"
#include "stm/MiniVector.hpp"

namespace stm {
  /**
   *  When we're word logging we simply store address/value pairs in the
   *  ValueList.
   */
  class WordLoggingValueListEntry {
      void** addr;
      void* val;

    public:
      WordLoggingValueListEntry(void** a, void* v) : addr(a), val(v) {
      }

      /**
       *  When word logging, we just need to make sure that the value we logged
       *  wasn't inside the protected stack region. We assume that the stack is
       *  at least word-aligned.
       */
      bool isValidFiltered(void** stack_low, void** stack_high) const {
          // can't be invalid on a transaction-local stack location.
          if (addr >= stack_low && addr < stack_high)
              return true;
          return isValid();
      }

      bool isValid() const {
          return *addr == val;
      }
  };

  /**
   *  When we're byte-logging we store a third word, the mask, and use it in the
   *  isValid() operation. The value we store is stored in masked form, which is
   *  an extra operation of overhead for single-threaded execution, but saves us
   *  masking during validation.
   */
  class ByteLoggingValueListEntry {
      void** addr;
      void* val;
      uintptr_t mask;

    public:
      ByteLoggingValueListEntry(void** a, void* v, uintptr_t m)
          : addr(a), val(v), mask(m) {
      }

      /**
       *  When we're dealing with byte-granularity we need to check values on a
       *  per-byte basis.
       *
       *  We believe that this implementation is safe because the logged address
       *  is *always* word aligned, thus promoting subword loads to aligned word
       *  loads followed by a masking operation will not cause any undesired HW
       *  behavior (page fault, etc.).
       *
       *  We're also assuming that the masking operation means that any
       *  potential "low-level" race that we introduce is immaterial---this may
       *  or may not be safe in C++1X. As an example, someone is
       *  nontransactionally writing the first byte of a word and we're
       *  transactionally reading the second byte. There is no language-level
       *  race, however when we promote the transactional byte read to a word,
       *  we read the same location the nontransactional access is writing, and
       *  there is no intervening synchronization. We're safe from some bad
       *  behavior because of the atomicity of word-level accesses, and we mask
       *  out the first byte, which means the racing read was actually
       *  dead. There are no executions where the source program can observe
       *  the race and thus they conclude that it is race-free.
       *
       *  I don't know if this argument is valid, but it is certainly valid for
       *  now, since there is no memory model for C/C++.
       *
       *  If this becomes a problem we can switch to a loop-when-mask != ~0x0
       *  approach.
       *
       *  When the address falls into the transaction's stack space we don't
       *  want to validate the value in case we performed an in-place write to
       *  it.
       */
      bool isValidFiltered(void** stack_low, void** stack_high) const {
          // can't be invalid on a transaction-local stack location.
          if (addr >= stack_low && addr < stack_high)
              return true;
          return isValid();
      }

      bool isValid() const {
          return ((uintptr_t)val & mask) == ((uintptr_t)*addr & mask);
      }
  };

  /**
   *  Hide the log isValid call behind a macro to deal with the
   *  STM_PROTECT_STACK macro.
   */
#if defined(STM_PROTECT_STACK)
#define STM_LOG_VALUE_IS_VALID(log, tx) \
      log->isValidFiltered(tx->stack_low, tx->stack_high);
#else
  #define STM_LOG_VALUE_IS_VALID(log, tx) \
      log->isValid();
#endif

  /**
   *  Hide the log type behind a macro do deal with the configuration.
   */
#if defined(STM_WS_WORDLOG) || defined(STM_USE_WORD_LOGGING_VALUELIST)
  typedef WordLoggingValueListEntry ValueListEntry;
#define STM_VALUE_LIST_ENTRY(addr, val, mask) ValueListEntry(addr, val)
#elif defined(STM_WS_BYTELOG)
  typedef ByteLoggingValueListEntry ValueListEntry;
#define STM_VALUE_LIST_ENTRY(addr, val, mask) ValueListEntry(addr, val, mask)
#else
#error "Preprocessor configuration error: STM_WS_(WORD|BYTE)LOG should be set"
#endif

  struct ValueList : public MiniVector<ValueListEntry> {
      ValueList(const unsigned long cap) : MiniVector<ValueListEntry>(cap) {
      }

#ifdef STM_PROTECT_STACK
      /**
       *  We override the minivector insert to track a "low water mark" for the
       *  stack address when we're stack filtering. The alternative is to do a
       *  range check here and avoid actually inserting values that are in the
       *  current local stack.
       */
      TM_INLINE void insert(ValueListEntry data, void**& low) {
          // we're inside the TM right now, so __builtin_frame_address is fine.
          low = (__builtin_frame_address(0) > low) ?
                    low : (void**)__builtin_frame_address(0);
          MiniVector<ValueListEntry>::insert(data);
      }
#define STM_LOG_VALUE(tx, addr, val, mask)                      \
      tx->vlist.insert(STM_VALUE_LIST_ENTRY(addr, val, mask), tx->stack_low);
#else
#define STM_LOG_VALUE(tx, addr, val, mask)                      \
      tx->vlist.insert(STM_VALUE_LIST_ENTRY(addr, val, mask));
#endif
  };
}

#endif // STM_VALUE_LIST_HPP
