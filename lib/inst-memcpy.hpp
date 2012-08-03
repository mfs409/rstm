/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_INST_MEMCPY_H
#define RSTM_INST_MEMCPY_H

#include "byte-logging.hpp"             // MaskedWord---always needed
#include "inst-common.hpp"              // base_of, offset_of

namespace stm {
  /**
   *  This template represents an N-word, stack allocated buffer that is used
   *  to match alignments, offsets, and possibly endiannes, during memcpy.
   */
  template <size_t N>
  class MemcpyBuffer {
      // need an extra word's worth of space, because we do sizeof(void*) chunk
      // operations, that may write garbage off the end of an N-word array.
      void* words[N + 1];
      size_t front;
      size_t back;

    public:
      MemcpyBuffer() : words(), front(0), back(0) {
      }

      bool canPut(const size_t n) const {
          return (n <= sizeof(words) - back);
      }

      bool canGet(const size_t n) const {
          return (n <= back - front);
      }

      void put(void* word, const size_t n) {
          assert(canPut(n) && "Not enough space for put");
          uint8_t* to = reinterpret_cast<uint8_t*>(words) + back;
          uint8_t* from = reinterpret_cast<uint8_t*>(word);
          memcpy(to, from, sizeof(word));
          back += n;
      }

      void* get(const size_t n) {
          assert(canGet(n) && "Not enough space for get");
          // uint8_t* address = bytes + front;
          // word = *reinterpret_cast<void**>(address);
          void* word;
          uint8_t* from = reinterpret_cast<uint8_t*>(words) + front;
          uint8_t* to = reinterpret_cast<uint8_t*>(&word);
          memcpy(to, from, sizeof(word));
          front += n;
          return word;
      }

      void rebase() {
          size_t n = back - front;
          assert(n < sizeof(void*) && "Write more bytes before rebase");
          if (n != 0) {
              uint8_t* from = reinterpret_cast<uint8_t*>(words) + front;
              uint8_t* to = reinterpret_cast<uint8_t*>(words);
              // can't overlap, can they? could use memmove if that is the case
              memcpy(to, from, sizeof(void*));
          }

          front = 0;
          back = n;
      }
  };

  /**
   *  This template encapsulates a memory region, and a word-granularity access
   *  function. It is used inside memcpy loops to read from memory into a
   *  buffer, or write from memory into a buffer.
   */
  template <typename Functor>
  struct MemcpyCursor {
    protected:
      void** addr;
      size_t offset;
      size_t remaining;
      Functor& f;

      size_t nextChunkSize() const {
          // how many bytes will our next access be?
          //
          // if (remaining < sizeof(void*) - offset)
          // then
          //   remaining
          // else
          //   sizeof(void*) - offset
          //
          // i.e., the minimum of the remaining bytes, and offset-adjusted word
          return min(sizeof(void*) - offset, remaining);
      }

      uintptr_t nextMask() const {
          return make_mask(offset, min(sizeof(void*), offset + remaining));
      }

    public:

      template <typename MemType> // why? deals with constness issues
      MemcpyCursor(MemType* addr, size_t n, Functor& f) :
          addr(base_of(addr)), offset(offset_of(addr)), remaining(n), f(f) {
      }

      bool complete() const {
          return !remaining;
      }

      MemcpyCursor<Functor>& operator++() {
          remaining = remaining - nextChunkSize();
          addr = addr + 1;
          offset = 0;                   // unilaterally 0 after first access
          return *this;
      }

      template <size_t N>
      bool tryPut(MemcpyBuffer<N>& buffer) {
          const size_t n = nextChunkSize();
          if (!buffer.canPut(n))
              return false;

          void* words[2];
          void* word;
          f(addr, words[0], nextMask());

          uint8_t* from = reinterpret_cast<uint8_t*>(words) + offset;
          uint8_t* to = reinterpret_cast<uint8_t*>(&word);
          memcpy(to, from, sizeof(word));

          buffer.put(word, n);
          return true;
      }

      template <size_t N>
      bool tryGet(MemcpyBuffer<N>& buffer) {
          const size_t n = nextChunkSize();
          if (!buffer.canGet(n))
              return false;

          void* words[2];
          void* word = buffer.get(n);

          uint8_t* from = reinterpret_cast<uint8_t*>(&word);
          uint8_t* to = reinterpret_cast<uint8_t*>(words) + offset;
          memcpy(to, from, sizeof(word));

          f(addr, words[0], nextMask());
          return true;
      }
  };

  /**
   *  This template represents an N-word, stack allocated buffer that is used
   *  to match alignments, offsets, and possibly endiannes, during
   *  memmove. It's complicated by the fact that we're operating in high->low
   *  order.
   */
  template <size_t N>
  class MemcpyReverseBuffer {
      // need an extra word's worth of space, because we do sizeof(void*) chunk
      // operations, that may write garbage off the front of an N-word array.
      union {
          uint8_t bytes[sizeof(void* [N + 1])];
          void* words[N + 1];
      };
      size_t front;
      size_t back;

    public:
      MemcpyReverseBuffer() : bytes(), front(sizeof(bytes)),
                              back(sizeof(bytes)) {
      }

      bool canPut(const size_t n) const {
          return (n <= front - sizeof(void*));
      }

      bool canGet(const size_t n) const {
          return (n <= back - front);
      }

      // useful bytes should be the high-order bytes in word
      void put(void* word, const size_t n) {
          assert(canPut(n) && "Not enough space for put");
          uint8_t* address = bytes + front - sizeof(void*);
          *reinterpret_cast<void**>(address) = word;
          front -= n;
      }

      // useful bytes will be the high-order bytes in word
      void get(void*& word, const size_t n) {
          assert(canGet(n) && "Not enough space for get");
          uint8_t* address = bytes + back - sizeof(void*);
          word = *reinterpret_cast<void**>(address);
          back -= n;
      }

      void rebase() {
          size_t n = back - front;
          assert(n < sizeof(void*) && "Write more bytes before rebase");
          // shift the unused bytes to the top of the buffer
          if (n != 0) {
              uint8_t* address = bytes + back - sizeof(void*);
              words[N] = *reinterpret_cast<void**>(address);
          }

          back = sizeof(bytes);
          front = back - n;
      }
  };

  /**
   *  This template encapsulates a memory region, and a word-granularity access
   *  function. It is used inside memcpy loops to read from memory into a
   *  buffer, or write from memory into a buffer.
   */
  template <typename Functor>
  struct MemcpyReverseCursor {
    protected:
      void** addr;
      size_t offset;
      size_t remaining;
      Functor& f;

      size_t nextChunkSize() const {
          // how many bytes will our next access be?
          //
          // if (remaining < offset)
          // then
          //   remaining
          // else
          //   offset
          //
          // i.e., the minimum of the remaining bytes, and offset
          return min(offset, remaining);
      }

      uintptr_t nextMask() const {
          return make_mask((offset > remaining) ? offset - remaining : 0,
                           offset);
      }

    public:

      template <typename MemType> // why template? deals with constness issues
      MemcpyReverseCursor(MemType* addr, size_t n, Functor& fun) :
          addr(base_of((void**)addr + n)), offset(offset_of((void**)addr + n)),
          remaining(n), f(fun) {
      }

      bool complete() const {
          return !remaining;
      }

      MemcpyReverseCursor<Functor>& operator++() {
          remaining = remaining - nextChunkSize();
          addr = addr - 1;
          offset = sizeof(void*);       // unilaterally after first access
          return *this;
      }

      template <size_t N>
      bool tryPut(MemcpyReverseBuffer<N>& buffer) {
          const size_t n = nextChunkSize();
          if (!buffer.canPut(n))
              return false;

          union {
              void* words[2];
              uint8_t bytes[sizeof(void*[2])];
          };

          f(addr, words[1], nextMask());
          buffer.put(*(void**)(bytes + offset), n);
          return true;
      }

      template <size_t N>
      bool tryGet(MemcpyReverseBuffer<N>& buffer) {
          const size_t n = nextChunkSize();
          if (!buffer.canGet(n))
              return false;

          union {
              void* words[2];
              uint8_t bytes[sizeof(void*[2])];
          };

          buffer.get(*(void**)(bytes + offset), n);
          f(addr, words[1], nextMask());
          return true;
      }
  };

  /**
   *  The basic low level memcpy loop is simple.
   */
  template <typename Reader, typename Writer, typename Buffer>
  static void memcpy_inner(Reader& read, Writer& write, Buffer& buffer)
  {
      // while we still have bytes to write
      // do
      //   while are bytes left to read, and there is buffer space
      //   do
      //     read into buffer
      //     increment read cursor
      //
      //   while there are enough bytes in the buffer to write
      //   do
      //     write from buffer
      //     increment write cursor
      //
      //   rebase the buffer to deal with remaining bytes
      while (!write.complete()) {
          while (!read.complete() && read.tryPut(buffer))
              ++read;

          while (write.tryGet(buffer))
              ++write;

          buffer.rebase();
      }
  }

  /**
   *  Memcpy forwards to the correct memcpy_inner.
   */
  template <typename ReadWord, typename WriteWord>
  static void* memcpy(void* dest, const void* src, size_t n,
                      ReadWord& r, WriteWord& w)
  {
      MemcpyBuffer<2> buffer;
      MemcpyCursor<ReadWord> read(src, n, r);
      MemcpyCursor<WriteWord> write(dest, n, w);
      memcpy_inner(read, write, buffer);
      return dest;
  }

  /**
   *  Memcpy_reverse forwards to the correct memcpy_inner.
   */
  template <typename ReadWord, typename WriteWord>
  static void* memcpy_reverse(void* dest, const void* src, size_t n,
                              ReadWord& r, WriteWord& w)
  {
      MemcpyReverseBuffer<2> buffer;
      MemcpyReverseCursor<ReadWord> read(src, n, r);
      MemcpyReverseCursor<WriteWord> write(dest, n, w);
      memcpy_inner(read, write, buffer);
      return dest;
  }

  struct NonTxRead {
      NonTxRead(TX*) {
      }

      void operator()(void** addr, void*& w, uintptr_t) {
          w = *addr;
      }
  };

  struct NonTxWrite {
      NonTxWrite(TX*) {
      }

      void operator()(void** addr, void*& w, uintptr_t mask) {
          MaskedWord::Write(addr, w, mask);
      }
  };
}

#endif // RSTM_INST_MEMCPY_H
