/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_INST_H
#define RSTM_INST_H

#include "byte-logging.hpp"             // Word, MaskedWord, LogWordType
#include "inst-readonly.hpp"            // CheckWritesetForReadOnly
#include "inst-stackfilter.hpp"         // NoFilter
#include "inst-writer.hpp"              // Writer<>
#include "inst-reader.hpp"              // Reader<>
#include "inst-alignment.hpp"           // Aligned<,>
#include "inst-common.hpp"              // make_mask, min
#include "inst-memcpy.hpp"              // MemcpyCursor, MemcpyBuffer, etc

// This is all inline stuff that shouldn't ever be visible from outside of the
// source file in which it's included, so we stick it all in an anonymouse
// namespace.
namespace {
  using namespace stm;

  /**
   *  This template and its specializations select between two types, based on
   *  the bool parameter.
   */
  template <bool S, typename F1, typename F2>
  struct Select {
      typedef F1 Result;
  };

  template <typename F1, typename F2>
  struct Select<false, F1, F2> {
      typedef F2 Result;
  };

  /**
   *  This template and its specializations select between two types, depending
   *  on which one is not NullType.
   *
   *  if (F1 != NullType)
   *      Result = F1
   *  else if (F2 != NullType)
   *      Result = F2
   *  else
   *      Error
   */
  template <typename F1, typename F2>
  struct SelectNonNull {
      typedef F1 Result;
  };

  template <typename F2>
  struct SelectNonNull<NullType, F2> {
      typedef F2 Result;
  };

  template <>
  struct SelectNonNull<NullType, NullType>;

  template <typename T,
            bool ForceAligned,
            typename WordType,
            typename IsReadOnly,
            typename ReadFilter,
            typename ReadRW,
            typename ReadReadOnly,
            typename WriteFilter,
            typename WriteRW,
            typename WriteReadOnly>
  class GenericInst {
    public:
     typedef GenericInst<T, ForceAligned, WordType, IsReadOnly, ReadFilter,
                         ReadRW, ReadReadOnly, WriteFilter, WriteRW,
                         WriteReadOnly> InstType;

    private:
      /**
       *  The number of words we need to reserve to deal with a T, is basically
       *  the number of bytes in a T divided by the number of bytes in a void*,
       *  plus one if a T* might not be aligned. The caveat is that we need at
       *  least one word for aligned subword types. The math below is evaluated
       *  at compile time.
       */
      enum {
          N = ((sizeof(T)/sizeof(void*)) ? sizeof(T)/sizeof(void*) : 1) +
          ((Aligned<T, ForceAligned>::value) ? 0 : 1)
      };

      /**
       *  Pick the correct read-only instrumentation (just in case the client
       *  uses NullType to indicate that there isn't any read-only-specific
       *  option).
       */
      typedef typename SelectNonNull<ReadReadOnly, ReadRW>::Result ReadRO;
      typedef typename SelectNonNull<WriteReadOnly, WriteRW>::Result WriteRO;

      /**
       *  Used by ITM to log values into the undo log. Supports the _ITM_LOG
       *  interface.
       */
      struct Logger {
          void operator()(void** addr, void* val, TX* tx, uintptr_t mask) const {
              tx->undo_log.insert(addr, val, mask);
          }

          void preWrite(TX*) {}
          void postWrite(TX*) {}
      };

      static inline size_t OffsetOf(const T* const addr) {
          return (Aligned<T, ForceAligned>::value) ? 0 : offset_of(addr);
      }

      static inline void** BaseOf(const T* const addr) {
          return (Aligned<T, ForceAligned>::value) ? (void**)addr : base_of(addr);
      }

      /**
       *  Allocate a buffer on the stack, allowing us to deal with multi-word
       *  and/or misaligned accesses.
       */
      class Buffer {
          void* words_[N];
          size_t offset_;

        public:
          Buffer(T* addr) : words_(), offset_(OffsetOf(addr)) {
          }

          void set(T val) {
              uint8_t* bytes = reinterpret_cast<uint8_t*>(words_) + offset_;
              uint8_t* p = reinterpret_cast<uint8_t*>(&val);
              memcpy(bytes, p, sizeof(val));
          }

          T get() {
              T val;
              uint8_t* bytes = reinterpret_cast<uint8_t*>(words_) + offset_;
              uint8_t* p = reinterpret_cast<uint8_t*>(&val);
              memcpy(p, bytes, sizeof(val));
              return val;
          }

          void*& operator[](int i) {
              return words_[i];
          }
      };

      /**
       *  This is the fundamental loop used for both chunked read and write
       *  access. It's basic job is to loop through each word in the buffer,
       *  and perform f() on it. This should be customized and inlined for each
       *  type of F, and N (and possibly OffsetOf and BaseOf) is a compile-time
       *  constant, so this should be optimized nicely.
       */
      template <typename F>
      static void ProcessWords(T* addr, Buffer& buf, F f) {
          // get the base and the offset of the address, in case we're dealing
          // with a sub-word or unaligned access. BaseOf and OffsetOf return
          // constants whenever they can.
          void** const base = BaseOf(addr);
          const size_t off = OffsetOf(addr);
          const size_t end = off + sizeof(T);

          // some algorithms want to do special stuff before performing a
          // potentially N > 1 word access.
          f.preAccess();

          // deal with the first word (there's always at least one)
          f(base, buf[0], make_mask(off, min(sizeof(void*), end)));

          // deal with any middle words for large types
          for (size_t i = 1, e = N - 1; i < e; ++i)
              f(base + i, buf[i], ~0);

          // if we have a final word to read, do so
          if ((N > 1) && (end > sizeof(void*)))
              f(base + N - 1, buf[N - 1], make_mask(0, end % sizeof(void*)));

          // some algorithms want to do special stuff after accessing a
          // potentially N-word access.
          f.postAccess();
      }

    public:
      /**
       *  The client's read instrumentation, generally inlined into the
       *  externally visible read routine (alg_tm_read for the library API,
       *  _ITM_R<T> for the ITM API).
       */
      static T Read(T* addr) {
          TX* tx = Self;

          // Use the configured pre-filter to do an "in-place" access if we
          // need to.
          ReadFilter filter;
          if (filter(addr, tx))
              return *addr;

          // Allocate space on the stack to perform the chunks of the read.
          Buffer buffer(addr);

          // If this transaction is read-only, then we don't need to do RAW
          // checks and we should use the ReadRO function that we're
          // configured with. Otherwise, do RAW checks based on the configured
          // WordType.
          IsReadOnly readonly;
          if (readonly(tx)) {
              ProcessWords(addr, buffer, Reader<ReadRO, NullType>(tx));
          }
          else
              ProcessWords(addr, buffer, Reader<ReadRW, WordType>(tx));

          return buffer.get();
      }

      /**
       *  The client's write instrumentation, generally inlined into the
       *  externally visible write routine.
       */
      static void Write(T* addr, T val) {
          TX* tx = Self;

          // Use the configured pre-filter to do an "in-place" access if we
          // need to.
          WriteFilter filter;
          if (filter(addr, tx)) {
              *addr = val;
              return;
          }

          // Allocate space on the stack to perform the chunks of the write.
          Buffer buffer(addr);
          buffer.set(val);

          // If this transaction is readonly, then we use the configured
          // WriteRO functor, otherwise we use the Write functor.
          IsReadOnly readonly;
          if (readonly(tx))
              ProcessWords(addr, buffer, Writer<WriteRO>(tx));
          else
              ProcessWords(addr, buffer, Writer<WriteRW>(tx));
      }

      /**
       *  The client's log instrumentation, generally inlined into the
       *  externally visible log routines.
       */
      static void Log(T* addr) {
          TX* tx = Self;

          // We don't filter stack logs---presumably there is a reason that the
          // compiler has generated a log of the transactional stack. This will
          // cause issues for rollback loops, if the address corrupts the stack
          // in a way that impacts the pre-longjmp execution.

          // Allocate space on the stack to perform the log.
          Buffer buffer(addr);
          buffer.set(*addr);

          // repurpose the undo log for logging.
          ProcessWords(addr, buffer, Writer<Logger>(tx));
      }

      /**
       *  Support the ITM Memcpy interface. This is part of the GenericInst
       *  class so that we have access to it's type parameters.
       *
       *  ReadTx: true if we want to read transactionally
       *  WriteTx: true if we want to write transactionally
       */
      template <bool ReadTransactionally, bool WriteTransactionally>
      static void* Memcpy(void* dest, const void* src, size_t n) {
          // The GenericInst class defines the transactional reader and writer
          // functions. For now, we don't customize for read-only operation.
          typedef Reader<ReadRW, WordType> TxRead;
          typedef Writer<WriteRW> TxWrite;

          // Select the read and write functors for the generic stm::memcpy
          // template based on the Memcpy parameters. The NonTxRead/Write
          // policies are defined in inst-memcpy, and just provide naked,
          // potentially masked operations using the expected policy
          // interface.
          typedef typename Select<ReadTransactionally,
                                  TxRead, NonTxRead>::Result ReadWord;
          typedef typename Select<WriteTransactionally,
                                  TxWrite, NonTxWrite>::Result WriteWord;

          // Actually call the correct specialization of stm::memcpy.
          TX* tx = Self;
          ReadWord read(tx);
          WriteWord write(tx);
          return memcpy(dest, src, n, read, write);
      }

      /**
       *  Support the ITM Memmove interface. This is part of the GenericInst
       *  class so that we have access to it's type parameters.
       *
       *  As with normal memmove, we can just use memcpy if the src pointer is
       *  higher than the dest (we have always read the locations we might be
       *  overwriting). If the src address is lower than the destination, then
       *  we use the same idea as memcpy, except that we need to move
       *  back-front through the src buffer.
       *
       *  ReadTx: true if we want to read transactionally
       *  WriteTx: true if we want to write transactionally
       */
      template <bool ReadTransactionally, bool WriteTransactionally>
      static void* Memmove(void* dest, const void* src, size_t n) {
          // The GenericInst class defines the transactional reader and writer
          // functions. For now, we don't customize for read-only operation.
          typedef Reader<ReadRW, WordType> TxRead;
          typedef Writer<WriteRW> TxWrite;

          // Select the read and write functors for the generic stm::memcpy
          // template based on the Memcpy parameters. The NonTxRead/Write
          // policies are defined in inst-memcpy, and just provide naked,
          // potentially masked operations using the expected policy
          // interface.
          typedef typename Select<ReadTransactionally,
                                  TxRead, NonTxRead>::Result ReadWord;
          typedef typename Select<WriteTransactionally,
                                  TxWrite, NonTxWrite>::Result WriteWord;

          // Actually call the correct specialization of stm::memcpy, or
          // memcpy_reverse, where necessary.
          TX* tx = Self;
          ReadWord read(tx);
          WriteWord write(tx);
          return (src > dest) ? memcpy(dest, src, n, read, write) :
              memcpy_reverse(dest, src, n, read, write);
      }

      /**
       *  Support ITM's transactional memset. Just loops through aligned words,
       *  doing low level writes.
       */
      static void Memset(void* target, int src, size_t n) {
          Writer<WriteRW> write(Self);

          // can only do aligned, masked accesses
          void** addr = base_of(target);
          size_t off = offset_of(target);

          // the union just makes it easy to "splat" the uint8_t represented by
          // src into a word.
          const union {
              uint8_t bytes[sizeof(void*)];
              void* word;
          } pattern = {{(uint8_t)src}};

          // perform writes while there are bytes left to write.
          while (n) {
              const size_t to_write = min(sizeof(void*) - off, n);
              write(addr, pattern.word, make_mask(off, off + to_write));
              n -= to_write;
              ++addr;
              off = 0;                  // unilaterally, after first iteration
          }
      }
  };

  /**
   *  Many of our Lazy TMs use the same instrumentation configuration, other
   *  than the read algorithm. This "Lazy" adapter fixes the type parameters of
   *  the GenericInst that they don't care about.
   *
   *  Barriers can be instantiated like Lazy<uint8_t, ReadAlg>::RSTM::Read().
   */
  template <typename T, typename Read>
  struct Lazy {
      struct BufferedWrite {
          void __attribute__((noinline))
          operator()(void** addr, void* val, TX* tx, uintptr_t mask) const {
              tx->writes.insert(addr, val, mask);
          }

          void preWrite(TX*) {}
          void postWrite(TX*) {}
      };

      typedef GenericInst<T, true, Word, CheckWritesetForReadOnly,
                          NoFilter, Read, NullType,
                          NoFilter, BufferedWrite, NullType> RSTM;

      typedef GenericInst<T, false, LoggingWordType, CheckWritesetForReadOnly,
                          FullFilter, Read, NullType,
                          FullFilter, BufferedWrite, NullType> ITM;
  };
}

#endif // RSTM_INST_H
