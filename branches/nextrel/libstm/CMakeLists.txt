#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information

include (AppendProperty)
  
if (CMAKE_SYSTEM_NAME MATCHES "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  append_property(SOURCE algs/bytelazy.cpp COMPILE_FLAGS -fno-strict-aliasing)
endif ()

set(sources
  txthread.cpp
  inst.cpp
  types.cpp
  profiling.cpp
  WBMMPolicy.cpp
  irrevocability.cpp
  algs/algs.cpp
  algs/biteager.cpp
  algs/biteagerredo.cpp
  algs/bitlazy.cpp
  algs/byear.cpp
  algs/byeau.cpp
  algs/byteeager.cpp
  algs/byteeagerredo.cpp
  algs/bytelazy.cpp
  algs/cgl.cpp
  algs/ctoken.cpp
  algs/ctokenturbo.cpp
  algs/llt.cpp
  algs/mcs.cpp
  algs/nano.cpp
  algs/norec.cpp
  algs/norecprio.cpp
  algs/oreau.cpp
  algs/orecala.cpp
  algs/oreceager.cpp
  algs/oreceagerredo.cpp
  algs/orecela.cpp
  algs/orecfair.cpp
  algs/oreclazy.cpp
  algs/pipeline.cpp
  algs/profiletm.cpp
  algs/ringala.cpp
  algs/ringsw.cpp
  algs/serial.cpp
  algs/profileapp.cpp
  algs/swiss.cpp
  algs/ticket.cpp
  algs/tli.cpp
  algs/tml.cpp
  algs/tmllazy.cpp
  policies/cbr.cpp
  policies/policies.cpp
  policies/static.cpp
  )

# Solaris includes a 16-bit implementation of rand_r, which isn't adequate for
# what we want to do, so we have a replacement.
if (CMAKE_SYSTEM_NAME MATCHES "SunOS")
  list (APPEND sources ${CMAKE_SOURCE_DIR}/alt-license/rand_r.cpp)
endif ()

set(mmapwrapsources hooks.cpp)
  
# build 32 and/or 64 bit libraries for intercepting munmap, so that we can
# avoid a problem when nontransactional code munmaps privatized memory that
# is still being accessed by doomed transactions
foreach (arch ${rstm_archs})
  add_library(mmapwrapper${arch} SHARED ${mmapwrapsources})
  append_property(TARGET mmapwrapper${arch} COMPILE_FLAGS -m${arch})
  append_property(TARGET mmapwrapper${arch} LINK_FLAGS -m${arch})
endforeach ()

#  Build the RSTM library
foreach (arch ${rstm_archs})
  add_library(stm${arch} STATIC ${sources})
  append_property(TARGET stm${arch} COMPILE_FLAGS -m${arch})
  if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_libraries(stm${arch} -lrt)
  endif ()
  if (CMAKE_SYSTEM_NAME MATCHES "SunOS")
    target_link_libraries(stm${arch} -lmtmalloc)
  endif ()
endforeach ()

