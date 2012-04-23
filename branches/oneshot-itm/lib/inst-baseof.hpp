#ifndef RSTM_INST_BASEOF_H
#define RSTM_INST_BASEOF_H

#include <stdint.h>

namespace stm {
  /**
   *  Addresses for everything other than aligned word and multiword accesses
   *  may need to be adjusted to a word boundary.
   */
  template <typename T,
            bool Aligned,
            size_t M = sizeof(T) % sizeof(void*)>
  struct Base {
      static inline void** BaseOf(T* addr) {
          const uintptr_t MASK = ~static_cast<uintptr_t>(sizeof(void*) - 1);
          const uintptr_t base = reinterpret_cast<uintptr_t>(addr) & MASK;
          return reinterpret_cast<void**>(base);
      }
  };

  /**
   *  Aligned words (or multiples of words) don't need to be adjusted.
   */
  template <typename T>
  struct Base<T, true, 0> {
      static inline void** BaseOf(T* addr) {
          return reinterpret_cast<void**>(addr);
      }
  };
}

#endif // RSTM_INST_BASEOF_H
