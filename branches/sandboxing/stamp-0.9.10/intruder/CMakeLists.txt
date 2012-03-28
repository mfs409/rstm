#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information

set(
  sources
  ../lib/list.cpp
  ../lib/mt19937ar.c
  ../lib/pair.c
  ../lib/queue.c
  ../lib/random.c
  ../lib/rbtree.cpp
  ../lib/thread.cpp
  ../lib/vector.cpp
  decoder.cpp
  detector.c
  dictionary.c
  intruder.c
  packet.c
  preprocessor.c
  stream.cpp)

# build the different executables
foreach (arch ${rstm_archs})
  add_stamp_executable(exec intruder ${arch} ${sources})
  add_target_definitions(${exec} MAP_USE_RBTREE)
endforeach ()

configure_file (sandbox.mk.cmake sandbox.mk @ONLY)