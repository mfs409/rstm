#ifndef RSTM_INST_OFFSETOF_H
#define RSTM_INST_OFFSETOF_H

namespace stm {
  /**
   *  We need to know the offset within a word for verything other than
   *  aligned words or multiword accesses.
   */
  template <typename T,
            bool Aligned,
            size_t M = sizeof(T) % sizeof(void*)>
  struct Offset {
      static inline size_t OffsetOf(const T* const addr) {
          const uintptr_t MASK = static_cast<uintptr_t>(sizeof(void*) - 1);
          const uintptr_t offset = reinterpret_cast<uintptr_t>(addr) & MASK;
          return static_cast<size_t>(offset);
      }
  };

  /**
   *  Aligned word and multiword accessed have a known offset of 0.
   */
  template <typename T>
  struct Offset<T, true, 0> {
      static inline size_t OffsetOf(T* addr) {
          return 0;
      }
  };
}

#endif // RSTM_INST_OFFSETOF_H
