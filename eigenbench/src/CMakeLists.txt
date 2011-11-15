#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science,
#  Lehigh University Department of Computer Science and Engineering
#    and
#  University of Otago Department of Computer Science
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information

set(
  sources
  eigenbench.c
  thread.c
  main.c)

# build the different executables
foreach (arch ${rstm_archs})
  add_stamp_executable(exec eigenbench-rstm ${arch} ${sources})
  add_target_definitions(${exec} RSTM)
endforeach ()
