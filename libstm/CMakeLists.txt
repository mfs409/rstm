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
  algs/wealth.cpp
  algs/cohorts.cpp
  algs/cohortsnoorder.cpp
  algs/cohortsold.cpp
  algs/cohortslazy.cpp
  algs/cohortseager.cpp
  algs/cohortsli.cpp
  algs/cohortsnorec.cpp
  algs/cohortsln.cpp
  algs/cohortsfilter.cpp
  algs/cohortslf.cpp
  algs/cohortsef.cpp                
  algs/fastlane.cpp
  algs/pessimistic.cpp
  policies/cbr.cpp
  policies/policies.cpp
  policies/static.cpp
  )

foreach (arch ${rstm_archs})
  add_library(stm${arch} ${sources})
  append_property(TARGET stm${arch} COMPILE_FLAGS -m${arch})
  if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_libraries(stm${arch} -lrt)
  endif ()
  if (CMAKE_SYSTEM_NAME MATCHES "SunOS")
    target_link_libraries(stm${arch} -lmtmalloc)
  endif ()
  if (libstm_use_pmu MATCHES "ON")
    target_link_libraries(stm${arch} -lpapi)
  endif ()
endforeach ()
