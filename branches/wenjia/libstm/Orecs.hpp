/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef ORECS_HPP__
#define ORECS_HPP__

#include "MiniVector.hpp"
#include "Globals.hpp"

namespace stm
{
  /**
   *  id_version_t uses the msb as the lock bit.  If the msb is zero, treat
   *  the word as a version number.  Otherwise, treat it as a struct with the
   *  lower 8 bits giving the ID of the lock-holding thread.
   */
  union id_version_t
  {
      struct
      {
          // ensure msb is lock bit regardless of platform
#if defined(STM_CPU_X86) || defined(STM_CPU_ARMV7) /* little endian */
          uintptr_t id:(8*sizeof(uintptr_t))-1;
          uintptr_t lock:1;
#else /* big endian (probably SPARC) */
          uintptr_t lock:1;
          uintptr_t id:(8*sizeof(uintptr_t))-1;
#endif
      } fields;
      uintptr_t all; // read entire struct in a single load
  };

  /**
   * When we acquire an orec, we may ultimately need to reset it to its old
   * value (if we abort).  Saving the old value with the orec is an easy way to
   * support this need without having exta logging in the descriptor.
   */
  struct orec_t
  {
      volatile id_version_t v; // current version number or lockBit + ownerId
      volatile uintptr_t    p; // previous version number
  };

  extern orec_t        orecs[NUM_ORECS];             // set of orecs

  /**
   *  Nano requires that we log not just the orec address, but also its value
   */
  struct nanorec_t
  {
      orec_t* o;   // address of the orec
      uintptr_t v; // value of the orec
      nanorec_t(orec_t* _o, uintptr_t _v) : o(_o), v(_v) { }
  };

  typedef MiniVector<orec_t*>      OrecList;     // vector of orecs
  typedef MiniVector<nanorec_t>    NanorecList;  // <orec,val> pairs

  extern orec_t        nanorecs[NUM_NANORECS];        // for Nano

  /**
   *  These simple functions are used for common operations on the global
   *  metadata arrays
   *
   *  [mfs] Should these be in some other file?  Should this be 'globals.hpp'?
   */

  /**
   *  Map addresses to orec table entries
   */
  inline orec_t* get_orec(void* addr)
  {
      uintptr_t index = reinterpret_cast<uintptr_t>(addr);
      return &orecs[(index>>3) % NUM_ORECS];
  }

  /**
   *  Map addresses to nanorec table entries
   */
  inline orec_t* get_nanorec(void* addr)
  {
      uintptr_t index = reinterpret_cast<uintptr_t>(addr);
      return &nanorecs[(index>>3) % NUM_NANORECS];
  }

}

#endif // ORECS_HPP__
