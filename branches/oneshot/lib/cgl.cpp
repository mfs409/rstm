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
#include "tx.hpp"
#include "platform.hpp"
#include "locks.hpp"
#include "metadata.hpp"

namespace stm
{
  /**
   * The only metadata we need is a single global padded lock
   */
  pad_word_t timestamp = {0};

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
      ++tx->commits_rw;
  }

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
  TM_FASTCALL
  void* tm_read(void** addr)
  {
      return *addr;
  }

  /**
   *  CGL write
   */
  TM_FASTCALL
  void tm_write(void** addr, void* val)
  {
      *addr = val;
  }

  scope_t* rollback(TX* tx)
  {
      assert(0 && "Rollback not supported in CGL");
      exit(-1);
      return NULL;
  }
}
