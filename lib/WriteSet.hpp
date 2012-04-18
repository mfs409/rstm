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

#ifdef STM_CC_SUN
#include <string.h>
#include <stdlib.h>
#else
#include <cstdlib>
#include <cstring>
#endif
#include <cassert>
#include <climits>
#include <stdint.h>
#include <cstdio>

namespace stm {
  /**
   *  hash function is straight from CLRS (that's where the magic constant
   *  comes from).
   */
  template <size_t S>
  static uint32_t hash(void**, const int);

  template <>
  uint32_t hash<4>(void** key, const int shift) {
      static const uint64_t M = 0x9E3779B9;
      uint64_t r = (((uintptr_t)key)) * M;
      return (uint32_t)((r & 0xFFFFFFFF) >> shift);
  }

  template <>
  uint32_t hash<8>(void** key, const int shift) {
      static const uint64_t M = 0x9E3779B97F4A782F;
      uint64_t r = (((uintptr_t)key)) * M;
      return (uint32_t)((r & 0xFFFFFFFF) >> shift);
  }

  /** The write set is an indexed array of elements. */
  template <typename WordType>
  class GenericWriteSet
  {
      /** data type for the index */
      struct IndexType {
          size_t version;
          void** address;
          int index;

          IndexType() : version(0), address(NULL), index(0) {
          }
      };

      IndexType* index;                 // hash table
      uint32_t shift;                   // for the hash function
      uint32_t ilength;                 // max size of hash
      size_t version;                   // version for fast clearing

      /** data type for the list */
      struct ListType {
          void** address;
          WordType value;

          ListType() : address(NULL), value() {
          }

          ListType(void** addr, const WordType& val) : address(addr),
                                                       value(val) {
          }

          void merge(const WordType& rhs) {
              value.merge(rhs);
          }

          void redo() const {
              value.writeTo(address);
          }

          void* getValue() const {
              return value.value();
          }

          uintptr_t getMask() const {
              return value.mask();
          }
      };

      ListType* list;                   // the array of actual data
      size_t capacity;                  // max array size
      size_t lsize;                     // elements in the array

      /**
       *  This doubles the size of the index. This *does not* do anything as
       *  far as actually doing memory allocation. Callers should delete[] the
       *  index table, increment the table size, and then reallocate it.
       */
      size_t __attribute__((noinline)) doubleIndexLength() {
          assert(shift != 0 &&
                 "ERROR: the writeset doesn't support an index this large");
          shift -= 1;
          ilength = 1 << (8 * sizeof(uint32_t) - shift);
          return ilength;
      }

      /** Rebuilds the index when required. */
      void __attribute__((noinline)) rebuild() {
          assert(version != 0 && "ERROR: the version should *never* be 0");

          // extend the index
          delete[] index;
          index = new IndexType[doubleIndexLength()];

          for (int i = 0, e = lsize; i < e; ++i) {
              void** address = list[i].address;
              uint32_t h = hash<sizeof(void*)>(address, shift);

              // search for the next available slot
              while (index[h].version == version)
                  h = (h + 1) % ilength;

              index[h].address = address;
              index[h].version = version;
              index[h].index = i;
          }
      }

      /** Grow the number of writeset entries. */
      void __attribute__((noinline)) resize() {
          ListType* temp = list;
          capacity = capacity * 2;
          list = new ListType[capacity];
          __builtin_memcpy(list, temp, sizeof(ListType) * lsize);
          delete[] temp;
      }

      /** Deals with version overflow. */
      size_t __attribute__((noinline)) resetOverflow() {
          __builtin_memset(index, 0, sizeof(IndexType) * ilength);
          return (version = 1);
      }

    public:
      GenericWriteSet(int init) : index(NULL), shift(8 * sizeof(uint32_t)),
                                  ilength(0), version(1), list(NULL),
                                  capacity(init), lsize(0) {
          // find a "good" index size for the initial capacity of the list
          while (doubleIndexLength() < 3 * init)
              ;
          index = new IndexType[ilength];
          list = new ListType[init];
      }

      ~GenericWriteSet() {
          delete[] index;
          delete[] list;
      }

      /**
       *  Search function.  The log is an in/out parameter, and the bool tells
       *  if the search succeeded. When we are byte-logging, the log's mask is
       *  updated to reflect the bytes in the returned value that are valid. In
       *  the case that we don't find anything, the mask is set to 0.
       */
      uintptr_t find(void** addr, void*& value) {
          uint32_t h = hash<sizeof(void*)>(addr, shift);

          while (index[h].version == version) {
              if (index[h].address != addr) {
                  h = (h + 1) % ilength; // continue probing
              }
              else {
                  value = list[index[h].index].getValue();
                  return list[index[h].index].getMask();
              }
          }
          return 0;
      }

      /**
       *  Encapsulate writeback in this routine, so that we can avoid making
       *  modifications to lots of STMs when we need to change writeback for a
       *  particular compiler.
       */
      void redo() const {
          for (uintptr_t i = 0, e = lsize; i < e; ++i)
              list[i].redo();
      }

      /**
       *  Inserts an entry in the write set.  Coalesces writes, which can
       *  appear as write reordering in a data-racy program.
       */
      void insert(void** addr, void* val, uintptr_t mask) {
          uint32_t h = hash<sizeof(void*)>(addr, shift);

          // Find the slot that this address should hash to. If we find it,
          // update the value. If we find an unused slot then it's a new
          // insertion.
          while (index[h].version == version) {
              if (index[h].address == addr) {
                  list[index[h].index].merge(WordType(val, mask));
                  return;
              }
              h = (h + 1) % ilength; // continue probing
          }

          // add the log to the list
          list[lsize].address = addr;
          list[lsize].value = WordType(val, mask);

          // update the index
          index[h].address = addr;
          index[h].version = version;
          index[h].index   = lsize;

          // update the end of the list
          lsize += 1;

          // resize the list if needed
          if (__builtin_expect(lsize == capacity, false))
              resize();

          // if we reach our load-factor
          // NB: load factor could be better handled rather than the magic
          //     constant 3 (used in constructor too)
          if (__builtin_expect((lsize * 3) >= ilength, false))
              rebuild();
      }

      /*** size() lets us know if the transaction is read-only */
      uintptr_t size() const {
          return lsize;
      }

      /**
       *  We use the version number to reset in O(1) time in the common case
       */
      void reset() {
          lsize = 0;
          version = (INT_MAX - version == 1) ? resetOverflow() : version + 1;
      }

      /*** Iterator interface: iterate over the list, not the index */
      typedef ListType* iterator;
      typedef const ListType* const_iterator;

      iterator begin() {
          return list;
      }

      const_iterator begin() const {
          return list;
      }

      iterator end() {
          return list + lsize;
      }

      const_iterator end() const {
          return list + lsize;
      }
  };
}

#endif // WRITESET_HPP__
