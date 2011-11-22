#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information

include (AppendProperty)

foreach (arch ${rstm_archs})
  add_library (ssigs${arch} SHARED ssigs.cpp)
  append_property(TARGET ssigs${arch} COMPILE_FLAGS -m${arch})
  if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_libraries(ssigs${arch} -ldl)
  endif ()
endforeach ()

# add_subdirectory (test)