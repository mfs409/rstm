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
 *  Since Apple doesn't support __thread in its toolchain, we need a clean
 *  interface that lets us use either __thread or pthread_getspecific.  This
 *  file hides all interaction with thread-local storage behind a simple
 *  macro, so that the complexities of non-__thread are hidden from the
 *  programmer
 *
 *  Note, too, that we allow a non-Apple user to configure the library in
 *  order to explicitly use pthread_getspecific.
 */

/**
 *  NB: This file could use significant hardening, using template
 *  metaprogramming to support all the necessary types, i.e., arrays, unions,
 *  etc.
 */

#ifndef STM_COMMON_THREAD_LOCAL_H
#define STM_COMMON_THREAD_LOCAL_H

#include <stm/config.h>

/**
 *  We define the following interface for interacting with thread local data.
 *
 *    THREAD_LOCAL_DECL_TYPE(X)
 *
 *  The macro will expand in a platform specific manner into the correct
 *  thread local type for X.
 *
 *  Examples:
 *
 *              static THREAD_LOCAL_DECL_TYPE(unsigned) a;
 *    Linux:    static __thread unsigned a;
 *    Windows:  static __declspec(thread) unsigned a;
 *    pthreads: static ThreadLocal<unsigned, sizeof(unsigned)> a;
 *
 *              extern THREAD_LOCAL_DECL_TYPE(Foo*) foo;
 *    Linux:    extern __thread Foo* foo;
 *    Windows:  extern __declspec(thread) Foo* foo;
 *    pthreads: extern ThreadLocal<Foo*, sizeof(Foo*)> a;
 */
#if defined(STM_TLS_PTHREAD)
# define THREAD_LOCAL_DECL_TYPE(X) stm::tls::ThreadLocal<X, sizeof(X)/sizeof(void*)>
#elif defined(STM_OS_WINDOWS)
# define THREAD_LOCAL_DECL_TYPE(X) __declspec(thread) X
#elif defined(STM_CC_GCC) || defined(STM_CC_SUN)
# define THREAD_LOCAL_DECL_TYPE(X) __thread X
#else
# warning "No thread local implementation defined."
#endif

/**
 *  In the above macro definitions, only STM_TLS_PTHREAD needs more work.
 *  The remainder of this file implements the ThreadLocal<> templates that
 *  make pthread_getspecific and pthread_setspecific look like __thread to
 *  client code
 */
#if defined(STM_TLS_PTHREAD)

#include <pthread.h>
#include <cstdlib>

namespace stm
{
  namespace tls
  {
    /**
     *  The basic thread local wrapper. The pthread interface stores the
     *  value as a void*, and this class manages that void* along with the
     *  pthread key.
     *
     *  NB: There are other ways to do this since all of the clients of this
     *      interface are template classes. This would save us a vtable
     *      pointer. The vtable pointer is really only there to support the
     *      destructor. We could simply call an interface function from the
     *      clients during their destruction. This would result in a more
     *      traditional policy-based implementation.
     */
    class PThreadLocalImplementation
    {
      protected:
        /**
         *  The constructor creates a pthread specific key and then assigns the
         *  incoming value to it.
         */
        PThreadLocalImplementation(void* const v)
        {
            pthread_key_create(&key, NULL);
            pthread_setspecific(key, v);
        }

        /**
         *  The destructor deletes the pthread key. Note that this is virtual
         *  because our /actual/ thread local implementations are all templates
         *  that inherit from this base class.
         */
        virtual ~PThreadLocalImplementation() {
            pthread_key_delete(key);
        }

        /** Returns the value stored with the key. */
        void* getValue() const {
            return pthread_getspecific(key);
        }

        /** Sets the value stored at the key. */
        void setValue(void* const v) {
            pthread_setspecific(key, v);
        }

      private:
        pthread_key_t key;

      private:
        // Do not implement.
        PThreadLocalImplementation();
        PThreadLocalImplementation(const PThreadLocalImplementation&);

        PThreadLocalImplementation&
        operator=(const PThreadLocalImplementation&);
    };

    /**
     *  Templates allow us to mimic an __thread interface to PThread data. We
     *  have two basic categories of data.
     *
     *   1) Value data.
     *   2) Pointer data.
     *
     *  Value data is builtin types and user defined structs that are
     *  compatible with direct __thread allocation. We can split this type of
     *  data into two cases.
     *
     *   1) Data that can fit in the size of a void*.
     *   2) Data that is too large.
     *
     *  This distinction is important when we consider levels of
     *  indirection. The pthread interface gives us access to void* sized slots
     *  of data. If we can fit what we need there, then we have just the one
     *  level of indirection to access it. If we can't, then we need to
     *  allocate space elsewhere for it, and store a pointer to that space in
     *  the slot.
     *
     *  Pointer data is easy to manage, since the client expects the location
     *  to look like a pointer, and pthreads is giving us a pointer. The client
     *  is going to have to manage the memory if it's dynamically allocated, so
     *  we can just return it as needed.
     *
     *  The main problem to this interface is that each interaction requires a
     *  pthread library call. If the client knew there was a pthreads interface
     *  (or just an interface more expensive than __thread) underneath then it
     *  could optimize for that situation.
     */

    /**
     *  The ThreadLocal template for objects that are larger than the size
     *  allotted by a pthread_getspecific. It uses either malloc and free or a
     *  trivial new constructor to allocate and deallocate space for the data,
     *  and memcpy to write to the data as needed. It owns the allocated space,
     *  which is fine because the client is thinking of this as automatically
     *  managed anyway.
     *
     *  Right now all the client can do is take the address of the thread local
     *  object, and access it through that pointer. If we need more
     *  functionality to make the ThreadLocal template easier to use, we can
     *  add it. In fact, the partial specializations for word and subword data
     *  /do/ have more functionality. This mismatch is not intentional, it's
     *  just demand-driven. Basically, a multi-word type is likely to be a
     *  struct type, where using a pointer is natural, while a word type is
     *  often a math type (like an integer) and an extended interface is more
     *  convenient.
     *
     *  NB: Currently, we trigger an error if the type has a constructor, but
     *      doesn't have a default constructor. This is the same approach that
     *      C++ takes with arrays of user-defined classes.
     *
     *      It's probably better to malloc and free the underlying space,
     *      rather than running the constructor/destructor.
     *
     *  NB: We know that this is for objects that are larger than a word
     *      because we provide partial template implementations for S=0 (< word
     *      sized) and S=1 (word sized).
     */
    template <typename T, unsigned S>
    class ThreadLocal : public PThreadLocalImplementation
    {
      public:
        ThreadLocal() : PThreadLocalImplementation(new T()) { }

        /**
         *  This constructor allocates a T and sets the stored key to be the
         *  address of the new T. It then uses memcpy to copy the bits of the
         *  passed T into the heap-allocated location. This avoids the problem
         *  that T might have a copy operator that does something odd.
         *
         *  The passed T should no longer be used after this constructor call,
         *  since the /actual/ thread local object is the heap object.
         */
        ThreadLocal(T t) : PThreadLocalImplementation(new T()) {
            __builtin_memcpy(getValue(), &t, S);
        }

        /** We "newed" the heap location so we delete it here. */
        virtual ~ThreadLocal() {
            delete static_cast<T*>(getValue());
        }

        /**
         *  Get the address of the thread local.
         *
         *  NB: This is the only way to interact with multi-word data at the
         *      moment.
         */
        T* operator&() const {
            return static_cast<T*>(getValue());
        }

      private:
        // Do not implement these. We assume that anyone trying to copy the
        // ThreadLocal object probably wants to copy the underlying object
        // instead.
        ThreadLocal(const ThreadLocal<T, S>&);
        ThreadLocal<T, S>& operator=(const ThreadLocal<T, S>&);
    };

    /**
     *  The ThreadLocal template for objects that are the size of a void*,
     *  but not a pointer. This differs from the basic template in that we
     *  don't need to allocate any extra space for the stored item.
     */
    template <typename T>
    class ThreadLocal<T, 1u> : public PThreadLocalImplementation
    {
      public:
        ThreadLocal() : PThreadLocalImplementation(NULL) {
        }

        /**
         *  The word-sized constructor casts the T to a void* and then just
         *  sets the stored value to that void*. This inhibits some type-based
         *  alias optimization, but we already know that pthreads has overhead
         *  that __thread doesn't.
         */
        ThreadLocal(T t) : PThreadLocalImplementation(NULL) {
            // Union performs safe cast. Alternative "reinterpret_cast" doesn't
            // work for all types (e.g., floating point types).
            union {
                T from;
                void* to;
            } cast = { t };
            setValue(cast.to);
        }

        /**
         *  Implicit conversion to a T. It's not obvious that this is the best
         *  option, but it's certainly the easiest. This lets us perform math
         *  on something like an integer without anything that we don't require
         *  for __thread use.
         *
         *  A more robust solution would be to use type-traits and extended
         *  template parameters, and add math operators to specializations
         *  where they make sense.
         */
        operator T() {
            // Union performs safe cast. Alternative "reinterpret_cast" doesn't
            // work for all types (e.g., floating point types).
            union {
                void* from;
                T to;
            } cast = { getValue() };
            return cast.to;
        }

        /** Assignment from a T. */
        ThreadLocal<T, 1u>& operator=(const T rhs) {
            union {
                T from;
                void* to;
            } cast = { rhs };
            setValue(cast.to);
            return *this;
        }

      private:
        // Do not implement these. We assume that anyone trying to copy the
        // ThreadLocal object probably wants to copy the underlying object
        // instead.
        ThreadLocal(const ThreadLocal<T, 1u>&);

        ThreadLocal<T, 1u>& operator=(const ThreadLocal<T, 1u>&);
    };

    /**
     *  The ThreadLocal template for objects that are less than the size of a
     *  void*.
     *
     *  NB: Currently unused by RSTM, which is why no interface is
     *      implemented.
     */
    template <typename T>
    class ThreadLocal<T, 0u> : public PThreadLocalImplementation {
      private:
        // Do not implement these. We assume that anyone that is trying to copy
        // the ThreadLocal object probably wants to copy the underlying object
        // instead.
        ThreadLocal(const ThreadLocal<T, 0u>&);
        ThreadLocal<T, 0u>& operator=(const ThreadLocal<T, 0u>&);
    };

    /**
     *  We use partial template specialization to implement a thread local
     *  type just for pointers. This extends the interface to allow
     *  interaction with the stored variable in "smart pointer" fashion.
     *
     *  This differs from the basic thread local implementation in that we
     *  don't provide an address-of operator, in the expectation that no one
     *  is going to want it, but we do provide an implicit cast to the
     *  underlying pointer type that returns the pointer value stored at the
     *  key.
     *
     *  This allows clients to pass and return the value as expected. A
     *  normal smart pointer would be hesitant to do this because of
     *  ownership issues, but this class is really just trying to emulate
     *  __thread. The ThreadLocal does *not* take ownership of managing the
     *  underlying pointer.
     */
    template <typename T>
    class ThreadLocal<T*, 1u> : public PThreadLocalImplementation
    {
      public:
        ThreadLocal(T* t = NULL) : PThreadLocalImplementation(t) { }

        virtual ~ThreadLocal() { }

        /**
         *  The smart pointer interface to the variable. These just perovide a
         *  cast-based interface that make the void* value that we store look
         *  like a T*.
         */
        const T& operator*() const {
            return *static_cast<T*>(getValue());
        }

        const T* operator->() const {
            return static_cast<T*>(getValue());
        }

        T& operator*() {
            return *static_cast<T*>(getValue());
        }

        operator T*() {
            return static_cast<T*>(getValue());
        }

        T* operator->() {
            return static_cast<T*>(getValue());
        }

        /** Assignment operator from a T*. */
        ThreadLocal<T*, 1u>& operator=(T* rhs) {
            setValue(rhs);
            return *this;
        }

        /**
         *  Test for equality with a T*... boils down to an address check. Is
         *  the right-hand-side equivalent to the pointer we're storing with
         *  the key?
         */
        bool operator==(T* rhs) {
            return (getValue() == rhs);
        }

      private:
        // Restrict access to potentially dangerous things. Start by
        // preventing the thread local to be copied around (presumably people
        // trying to copy a ThreadLocal /actually/ want to copy the
        // underlying object).
        ThreadLocal(const ThreadLocal<T*, 1u>&);

        ThreadLocal<T*, 1u>& operator=(const ThreadLocal<T*, 1u>&);
    };
  } // namespace stm::tls
} // namespace stm
#endif

#endif // STM_COMMON_THREAD_LOCAL_H
