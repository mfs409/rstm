/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <stdint.h>
#include <iostream>
#include <cstdlib>
#include "platform.hpp"
#include "locks.hpp"
#include "metadata.hpp"

namespace stm
{
  /**
   *  Store per-thread metadata.  There isn't much for CGL...
   */
  struct TX
  {
      /*** for flat nesting ***/
      int nesting_depth;

      /*** unique id for this thread ***/
      int id;

      /*** number of commits ***/
      int commits;

      /**
       *  Simple constructor for TX: zero all fields, get an ID
       */
      TX()
          : nesting_depth(0), commits(0)
      {
          id = faiptr(&threadcount.val);
          threads[id] = this;
      }
  };

  /*** The only metadata we need is a single global padded lock ***/
  pad_word_t timestamp = {0};

  /**
   *  No system initialization is required, since the timestamp is already 0
   */
  void tm_sys_init() { }

  /**
   *  When the transactional system gets shut down, we call this to dump
   *  stats for all threads
   */
  void tm_sys_shutdown()
  {
      static volatile unsigned int mtx = 0;
      while (!bcas32(&mtx, 0u, 1u)) { }
      for (uint32_t i = 0; i < threadcount.val; i++) {
          std::cout << "Thread: "    << threads[i]->id
                    << "; Commits: " << threads[i]->commits
                    << std::endl;
      }
      CFENCE;
      mtx = 0;
  }

  /**
   *  For querying to get the current algorithm name
   */
  const char* tm_getalgname() { return "CGL"; }

  /**
   *  Start a transaction: if we're already in a tx, bump the nesting
   *  counter.  Otherwise, grab the lock.
   */
  void tm_begin()
  {
      TX* tx = Self;
      if (++tx->nesting_depth > 1)
          return;
      tatas_acquire(&timestamp.val);
  }

  /**
   *  End a transaction: decrease the nesting level, then perhaps release the
   *  lock and increment the count of commits.
   */
  void tm_end()
  {
      TX* tx = Self;
      if (--tx->nesting_depth)
          return;
      tatas_release(&timestamp.val);
      ++tx->commits;
  }

  /**
   *  To initialize the thread's TM support, we need only ensure it has a
   *  descriptor.
   */
  void tm_thread_init()
  {
      // multiple inits from one thread do not cause trouble
      if (Self) return;

      // create a TxThread and save it in thread-local storage
      Self = new TX();
  }

  /**
   *  When a thread is done using the TM, we don't need to do anything
   *  special.
   */
  void tm_thread_shutdown() { }

  /**
   *  In CGL, malloc doesn't need any special care
   */
  void* tm_alloc(size_t s) { return malloc(s); }

  /**
   *  In CGL, free doesn't need any special care
   */
  void tm_free(void* p) { free(p); }

  /**
   *  CGL read
   */
  void* tm_read(void** addr)
  {
      return *addr;
  }

  /**
   *  CGL write
   */
  void tm_write(void** addr, void* val)
  {
      *addr = val;
  }

}
