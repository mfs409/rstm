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
 *  The RSTM backends that use redo logs all rely on this datastructure,
 *  which provides O(1) clear, insert, and lookup by maintaining a hashed
 *  index into a vector.
 */

#ifndef WRITESETENTRY_HPP__
#define WRITESETENTRY_HPP__

#include <cassert>
#include <stdint.h>

namespace stm
{
  /**
   *  The WriteSet implementation is heavily influenced by the configuration
   *  parameters, STM_WS_(WORD/BYTE)LOG, and STM_ABORT_ON_THROW. This means
   *  that much of this file is ifdeffed accordingly.
   */

  /**
   * The log entry type when we're word-logging is pretty trivial, and just
   * logs address/value pairs.
   */
  struct WordLoggingWriteSetEntry
  {
      void** addr;
      void*  val;

      WordLoggingWriteSetEntry(void** paddr, void* pval)
          : addr(paddr), val(pval)
      { }

      /**
       *  Called when we are WAW an address, and we want to coalesce the
       *  write. Trivial for the word-based writeset, but complicated for the
       *  byte-based version.
       */
      void update(const WordLoggingWriteSetEntry& rhs) { val = rhs.val; }

      /**
       * Check to see if the entry is completely contained within the given
       * address range. We have some preconditions here w.r.t. alignment and
       * size of the range. It has to be at least word aligned and word
       * sized. This is currently only used with stack addresses, so we don't
       * include asserts because we don't want to pay for them in the common
       * case writeback loop.
       */
      bool filter(void** lower, void** upper)
      {
          return !(addr + 1 < lower || addr >= upper);
      }

      /**
       * Called during writeback to actually perform the logged write. This is
       * trivial for the word-based set, but the byte-based set is more
       * complicated.
       */
      void writeback() const { *addr = val; }

      /**
       * Called during rollback if there is an exception object that we need to
       * perform writes to. The address range is the range of addresses that
       * we're looking for. If this log entry is contained in the range, we
       * perform the writeback.
       *
       * NB: We're assuming a pretty well defined address range, in terms of
       *     size and alignment here, because the word-based writeset can only
       *     handle word-sized data.
       */
      void rollback(void** lower, void** upper)
      {
          assert((uint8_t*)upper - (uint8_t*)lower >= (int)sizeof(void*));
          assert((uintptr_t)upper % sizeof(void*) == 0);
          if (addr >= lower && (addr + 1) <= upper)
              writeback();
      }
  };

  /**
   * The log entry for byte logging is complicated by
   *
   *   1) the fact that we store a bitmask
   *   2) that we need to treat the address/value/mask instance variables as
   *      both word types, and byte types.
   *
   *  We do this with unions, which makes the use of these easier since it
   *  reduces the huge number of casts we perform otherwise.
   *
   *  Union naming is important, since the outside world only directly deals
   *  with the word-sized fields.
   */
  struct ByteLoggingWriteSetEntry
  {
      union {
          void**   addr;
          uint8_t* byte_addr;
      };

      union {
          void*   val;
          uint8_t byte_val[sizeof(void*)];
      };

      union {
          uintptr_t mask;
          uint8_t   byte_mask[sizeof(void*)];
      };

      ByteLoggingWriteSetEntry(void** paddr, void* pval, uintptr_t pmask)
      {
          addr = paddr;
          val  = pval;
          mask = pmask;
      }

      /**
       *  Called when we are WAW an address, and we want to coalesce the
       *  write. Trivial for the word-based writeset, but complicated for the
       *  byte-based version.
       *
       *  The new value is the bytes from the incoming log injected into the
       *  existing value, we mask out the bytes we want from the incoming word,
       *  mask the existing word, and union them.
       */
      void update(const ByteLoggingWriteSetEntry& rhs)
      {
          // fastpath for full replacement
          if (__builtin_expect(rhs.mask == (uintptr_t)~0x0, true)) {
              val = rhs.val;
              mask = rhs.mask;
              return;
          }

          // bit twiddling for awkward intersection, avoids looping
          uintptr_t new_val = (uintptr_t)rhs.val;
          new_val &= rhs.mask;
          new_val |= (uintptr_t)val & ~rhs.mask;
          val = (void*)new_val;

          // the new mask is the union of the old mask and the new mask
          mask |= rhs.mask;
      }

      /**
       *  Check to see if the entry is completely contained within the given
       *  address range. We have some preconditions here w.r.t. alignment and
       *  size of the range. It has to be at least word aligned and word
       *  sized. This is currently only used with stack addresses, so we don't
       *  include asserts because we don't want to pay for them in the common
       *  case writeback loop.
       *
       *  The byte-logging writeset can actually accommodate awkward
       *  intersections here using the mask, but we're not going to worry about
       *  that given the expected size/alignment of the range.
       */
      bool filter(void** lower, void** upper)
      {
          return !(addr + 1 < lower || addr >= upper);
      }

      /**
       *  If we're byte-logging, we'll write out each byte individually when
       *  we're not writing a whole word. This turns all subword writes into
       *  byte writes, so we lose the original atomicity of (say) half-word
       *  writes in the original source. This isn't a correctness problem
       *  because of our transactional synchronization, but could be a
       *  performance problem if the system depends on sub-word writes for
       *  performance.
       */
      void writeback() const
      {
          if (__builtin_expect(mask == (uintptr_t)~0x0, true)) {
              *addr = val;
              return;
          }

          // mask could be empty if we filtered out all of the bytes
          if (mask == 0x0)
              return;

          // write each byte if its mask is set
          for (unsigned i = 0; i < sizeof(val); ++i)
              if (byte_mask[i] == 0xff)
                  byte_addr[i] = byte_val[i];
      }

      /**
       *  Called during the rollback loop in order to write out buffered writes
       *  to an exception object (represented by the address range). We don't
       *  assume anything about the alignment or size of the exception object.
       */
      void rollback(void** lower, void** upper)
      {
          // two simple cases first, no intersection or complete intersection.
          if (addr + 1 < lower || addr >= upper)
              return;

          if (addr >= lower && addr + 1 <= upper) {
              writeback();
              return;
          }

          // odd intersection
          for (unsigned i = 0; i < sizeof(void*); ++i) {
              if ((byte_mask[i] == 0xff) &&
                  (byte_addr + i >= (uint8_t*)lower ||
                   byte_addr + i < (uint8_t*)upper))
                  byte_addr[i] = byte_val[i];
          }
      }
  };

  /**
   *  Pick a write-set implementation, based on the configuration.
   */
#if defined(STM_WS_WORDLOG)
  typedef WordLoggingWriteSetEntry WriteSetEntry;
#   define STM_WRITE_SET_ENTRY(addr, val, mask) addr, val
#elif defined(STM_WS_BYTELOG)
  typedef ByteLoggingWriteSetEntry WriteSetEntry;
#   define STM_WRITE_SET_ENTRY(addr, val, mask) addr, val, mask
#else
#   error WriteSet logging granularity configuration error.
#endif
} // namespace stm

#endif // WRITESETENTRY_HPP__
