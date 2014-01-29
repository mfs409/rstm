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

#ifndef WRITESET_HPP__
#define WRITESET_HPP__

#include "WriteSetEntry.hpp"

namespace stm
{
  /**
   *  The write set is an indexed array of WriteSetEntry elements.  As with
   *  MiniVector, we make sure that certain expensive but rare functions are
   *  never inlined.
   */
  class WriteSet
  {
#if 0
      // [mfs] TODO: we should make a variant of the WriteSet that uses this
      //       instead of the magic constant "3"
      static const int SPILL_FACTOR = 8; // 8 probes and we resize the list...
#endif

      /***  data type for the index */
      struct index_t
      {
          size_t version;
          void*  address;
          size_t index;

          index_t() : version(0), address(NULL), index(0) { }
      };

      index_t* index;                             // hash entries
      size_t   shift;                             // for the hash function
      size_t   ilength;                           // max size of hash
      size_t   version;                           // version for fast clearing

      WriteSetEntry* list;                        // the array of actual data
      size_t   capacity;                          // max array size
      size_t   lsize;                             // elements in the array

      /**
       *  hash function is straight from CLRS (that's where the magic
       *  constant comes from).
       */
      size_t hash(void* const key) const
      {
          static const unsigned long long s = 2654435769ull;
          const unsigned long long r = ((unsigned long long)key) * s;
          return (size_t)((r & 0xFFFFFFFF) >> shift);
      }

      /**
       *  This doubles the size of the index. This *does not* do anything as
       *  far as actually doing memory allocation. Callers should delete[]
       *  the index table, increment the table size, and then reallocate it.
       */
      size_t doubleIndexLength();

      /**
       *  Supporting functions for resizing.  Note that these are never
       *  inlined.
       */
      void rebuild();
      void resize();
      void reset_internal();

    public:

      WriteSet(const size_t initial_capacity);
      ~WriteSet();

      /**
       *  Search function.  The log is an in/out parameter, and the bool
       *  tells if the search succeeded. When we are byte-logging, the log's
       *  mask is updated to reflect the bytes in the returned value that are
       *  valid. In the case that we don't find anything, the mask is set to 0.
       */
      bool find(WriteSetEntry& log) const
      {
          size_t h = hash(log.addr);

          while (index[h].version == version) {
              if (index[h].address != log.addr) {
                  // continue probing
                  h = (h + 1) % ilength;
                  continue;
              }
#if defined(STM_WS_WORDLOG)
              log.val = list[index[h].index].val;
              return true;
#elif defined(STM_WS_BYTELOG)
              // Need to intersect the mask to see if we really have a match. We
              // may have a full intersection, in which case we can return the
              // logged value. We can have no intersection, in which case we can
              // return false. We can also have an awkward intersection, where
              // we've written part of what we're trying to read. In that case,
              // the "correct" thing to do is to read the word from memory, log
              // it, and merge the returned value with the partially logged
              // bytes.
              WriteSetEntry& entry = list[index[h].index];
              if (__builtin_expect((log.mask & entry.mask) == 0, false)) {
                  log.mask = 0;
                  return false;
              }

              // The update to the mask transmits the information the caller
              // needs to know in order to distinguish between a complete and a
              // partial intersection.
              log.val = entry.val;
              log.mask = entry.mask;
              return true;
#else
#error "Preprocessor configuration error."
#endif
          }

#if defined(STM_WS_BYTELOG)
          log.mask = 0x0; // report that there were no intersecting bytes
#endif
          return false;
      }

      /**
       *  Support for abort-on-throw rollback tricky.  We might need to write
       *  to an exception object.
       *
       *  NB: We use a macro to hide the fact that some rollback calls are
       *      really simple.  This gets called by ~30 STM implementations
       */
#if !defined (STM_ABORT_ON_THROW)
      void rollback() { }
#   define STM_ROLLBACK(log, exception, len) log.rollback()
#else
      void rollback(void**, size_t);
#   define STM_ROLLBACK(log, exception, len) log.rollback(exception, len)
#endif

      /**
       *  Encapsulate writeback in this routine, so that we can avoid making
       *  modifications to lots of STMs when we need to change writeback for a
       *  particular compiler.
       */
      inline void writeback()
      {
          for (iterator i = begin(), e = end(); i != e; ++i)
              i->writeback();
      }

      /**
       *  Inserts an entry in the write set.  Coalesces writes, which can
       *  appear as write reordering in a data-racy program.
       */
      void insert(const WriteSetEntry& log)
      {
          size_t h = hash(log.addr);

          //  Find the slot that this address should hash to. If we find it,
          //  update the value. If we find an unused slot then it's a new
          //  insertion.
          while (index[h].version == version) {
              if (index[h].address != log.addr) {
                  h = (h + 1) % ilength;
                  continue; // continue probing at new h
              }

              // there /is/ an existing entry for this word, we'll be updating
              // it no matter what at this point
              list[index[h].index].update(log);
              return;
          }

          // add the log to the list (guaranteed to have space)
          list[lsize] = log;

          // update the index
          index[h].address = log.addr;
          index[h].version = version;
          index[h].index   = lsize;

          // update the end of the list
          lsize += 1;

          // resize the list if needed
          if (__builtin_expect(lsize == capacity, false))
              resize();

          // if we reach our load-factor
          // NB: load factor could be better handled rather than the magic
          //     constant 3 (used in constructor too).
          if (__builtin_expect((lsize * 3) >= ilength, false))
              rebuild();
#if 0
          // [mfs] I think we might want to consider resizing more frequently...
          if (__builtin_expect(spills > SPILL_FACTOR, false)) {
              printf("spillflow\n");
              rebuild();
          }
#endif
      }

      /*** size() lets us know if the transaction is read-only */
      size_t size() const { return lsize; }

      /*** will_reorg() lets us know if an insertion will cause a reorg of the data structure */
      bool will_reorg() const
      {
          size_t nsize = lsize + 1;
          return ((nsize == capacity) || ((nsize * 3) >= ilength));
      }

      /**
       *  We use the version number to reset in O(1) time in the common case
       */
      void reset()
      {
          lsize    = 0;
          version += 1;

          // check overflow
          if (version != 0)
              return;
          reset_internal();
      }

      /*** Iterator interface: iterate over the list, not the index */
      typedef WriteSetEntry* iterator;
      iterator begin() const { return list; }
      iterator end()   const { return list + lsize; }
  };
}

#endif // WRITESET_HPP__
