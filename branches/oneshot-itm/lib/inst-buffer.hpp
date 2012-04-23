#ifndef RSTM_INST_BUFFER_HPP
#define RSTM_INST_BUFFER_HPP

namespace stm {
  /**
   *  This template gets specialized to tell us how many word-size accesses
   *  need to be done in order to satisfy an access to the given type. This
   *  is a maximum size, it might be 1 word too big when the type isn't
   *  guaranteed to be aligned, but the address is actually aligned. We
   *  account for this in the loop that actually performs the read.
   */
  template <typename T,
            bool Aligned,
            size_t M = sizeof(T) % sizeof(void*)>
  struct Buffer;

  /** Aligned subword types only need 1 word. */
  template <typename T, size_t M>
  struct Buffer<T, true, M> {
      enum { Words = 1 };
  };

  /** Possibly unaligned subword types may need 2 words. */
  template <typename T, size_t M>
  struct Buffer<T, false, M> {
      enum { Words = 2 };
  };

  /**
   *  Aligned word and multiword types
   *
   *  NB: we assume that this will not be instantiated with multiword types
   *      that aren't multiples of the size of a word. We could check this
   *      with static-asserts, if we had them.
   */
  template <typename T>
  struct Buffer<T, true, 0> {
      enum { Words = sizeof(T)/sizeof(void*) };
  };

  /**
   *  Possibly unaligned word and multiword types may need an extra word.
   *
   *  NB: we assume that this will not be instantiated with multiword types
   *      that aren't multiples of the size of a word. We could check this
   *      with static-asserts, if we had them.
   */
  template <typename T>
  struct Buffer<T, false, 0> {
      enum { Words = sizeof(T)/sizeof(void*) + 1 };
  };
}

#endif // RSTM_INST_BUFFER_HPP
