#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information

#
# The names of the different STM apis that must be targeted when building
# benchmarks.  Note that this doesn't really work correctly with gcctm, as it
# results in us building and linking ALGNAMES different executables when we
# only need one
#
APIS = lockapi genericapi

#
# The names of the STM algorithms, and of all supporting compilable files
#
ALGNAMES = cgl norec tml cohortseager cohorts ctokenturbo ctoken llt \
           oreceagerredo orecela orecala oreclazy oreceager          \
           oreclazybackoff oreclazyhb oreclazyhour
