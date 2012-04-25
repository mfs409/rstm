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
 *  A simple vector-like templated collection object.  The main difference
 *  from the STL vector is that we can force uncommon code (such as resize)
 *  to be a function call by putting instantiations of the expand() method
 *  into their own .o file.
 */

#ifndef MINIVECTOR_HPP__
#define MINIVECTOR_HPP__

#include <cassert>
#include <cstdlib>
#include <string.h>
#include <iterator>
#include "platform.hpp"

namespace stm
{
  /***  Self-growing array */
  template <class T>
  class MiniVector
  {
      size_t m_cap;                     // current vector capacity
      size_t m_size;                    // current number of used elements
      T* m_elements;                    // the actual elements in the vector

      /*** double the size of the minivector */
      void expand();

    public:
      MiniVector() : m_cap(1), m_size(0),
                     m_elements(static_cast<T*>(malloc(sizeof(T)*m_cap)))
      {
      }

      /*** Construct a minivector with a default size */
      MiniVector(const unsigned long capacity)
          : m_cap(capacity), m_size(0),
            m_elements(static_cast<T*>(malloc(sizeof(T)*m_cap)))
      {
          assert(m_elements);
      }

      ~MiniVector() { free(m_elements); }

      /** std::vector interface */
      void reserve(size_t n);

      /*** Reset the vector without destroying the elements it holds */
      TM_INLINE void reset() { m_size = 0; }
      TM_INLINE void clear() { m_size = 0; } // used in redo-log

      /*** Insert an element into the minivector */
      TM_INLINE void insert(T data)
      {
          // NB: There is a tradeoff here.  If we put the element into the list
          // first, we are going to have to copy one more object any time we
          // double the list.  However, by doing things in this order we avoid
          // constructing /data/ on the stack if (1) it has a simple
          // constructor and (2) /data/ isn't that big relative to the number
          // of available registers.

          // Push data onto the end of the array and increment the size ("size"
          // register eliminates m_size reload")
          size_t size = m_size++;
          m_elements[size] = data;

          // If the list is full, double the list size, allocate a new array
          // of elements, bitcopy the old array into the new array, and free
          // the old array. No destructors are called.
          if (__builtin_expect(size + 1 >= m_cap, false))
              expand();
      }

      void push_back(T data) { insert(data); }

      T& operator[](int i) { return m_elements[i]; }
      const T& operator[](int i) const { return m_elements[i]; }

      /*** Simple getter to determine the array size */
      TM_INLINE unsigned long size() const { return m_size; }

      /*** iterator interface, just use a basic pointer */
      typedef T* iterator;
      typedef const T* const_iterator;
      typedef std::reverse_iterator<iterator> reverse_iterator;
      typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

      /*** iterator to the start of the array */
      TM_INLINE iterator begin() {
          return m_elements;
      }

      TM_INLINE const_iterator begin() const {
          return m_elements;
      }

      TM_INLINE reverse_iterator rend() {
          return reverse_iterator(begin());
      }

      TM_INLINE const_reverse_iterator rend() const {
          return const_reverse_iterator(begin());
      }

      /*** iterator to the end of the array */
      TM_INLINE iterator end() {
          return m_elements + m_size;
      }

      TM_INLINE const_iterator end() const {
          return m_elements + m_size;
      }

      TM_INLINE reverse_iterator rbegin() {
          return reverse_iterator(end());
      }

      TM_INLINE const_reverse_iterator rbegin() const {
          return const_reverse_iterator(end());
      }

  }; // class MiniVector

  /*** double the size of a minivector */
  template <class T>
  NOINLINE
  void MiniVector<T>::expand()
  {
      m_cap *= 2;                       // simple doubling
      T* temp = m_elements;
      m_elements = static_cast<T*>(malloc(sizeof(T) * m_cap));
      assert(m_elements);
      memcpy(m_elements, temp, sizeof(T)*m_size);
      free(temp);
  }

  template <class T>
  NOINLINE
  void MiniVector<T>::reserve(size_t n)
  {
      if (n <= m_cap)
          return;

      while (n > m_cap)
          m_cap *= 2;

      T* temp = m_elements;
      m_elements = static_cast<T*>(malloc(sizeof(T) * m_cap));
      assert(m_elements);
      memcpy(m_elements, temp, sizeof(T)*m_size);
      free(temp);
  }
} // stm

#endif // MINIVECTOR_HPP__
