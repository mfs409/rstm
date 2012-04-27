#ifndef INST_COMMON_H
#define INST_COMMON_H

namespace stm {
  /**
   *  Whenever we need to perform a transactional load or store we need a
   *  mask that has 0xFF in all of the bytes that we are intersted in. This
   *  computes a mask given an [i, j) range, where 0 <= i < j <=
   *  sizeof(void*).
   *
   *  NB: When the parameters are compile-time constants we expect this to
   *    become a simple constant in the binary when compiled with
   *    optimizations.
   */
  static uintptr_t make_mask(size_t i, size_t j) {
      // assert(0 <= i && i < j && j <= sizeof(void*) && "range is incorrect")
      uintptr_t mask = ~(uintptr_t)0;
      mask = mask >> (8 * (sizeof(void*) - j + i)); // shift 0s to the top
      mask = mask << (8 * i);                       // shift 0s into the bottom
      return mask;
  }

  static size_t min(size_t lhs, size_t rhs) {
      return (lhs < rhs) ? lhs : rhs;
  }

  template <typename T>
  static void** base_of(T* addr) {
      const uintptr_t MASK = ~static_cast<uintptr_t>(sizeof(void*) - 1);
      const uintptr_t base = reinterpret_cast<uintptr_t>(addr) & MASK;
      return reinterpret_cast<void**>(base);
  }

  template <typename T>
  static size_t offset_of(const T* const addr) {
      const uintptr_t MASK = static_cast<uintptr_t>(sizeof(void*) - 1);
      const uintptr_t offset = reinterpret_cast<uintptr_t>(addr) & MASK;
      return static_cast<size_t>(offset);
  }

}

#endif // INST_COMMON_H
