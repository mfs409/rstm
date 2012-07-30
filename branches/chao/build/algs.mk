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
# benchmarks
#
LOCKAPI   = lockapi
STMAPI    = stmapi
CUSTOMAPI = fptrapi
APIS      = $(LOCKAPI) $(STMAPI) $(CUSTOMAPI)

#
# The names of all the STM algorithms
#
STMALGS = NOrec TML CohortsEager Cohorts CTokenTurbo CToken LLT OrecEagerRedo \
          OrecELA OrecALA OrecLazy OrecEager NOrecHB OrecLazyBackoff          \
          OrecLazyHB OrecLazyHour NOrecBackoff NOrecHour OrecEagerHour        \
          OrecEagerHB OrecEagerBackoff CGL AdapTM

#
# The names of the entries in $(STMALGS) that are able to use the LOCKAPI
# instrumentation interface
#
LOCKALGS = CGL

#
# The names of the entries in $(STMALGS) that are able to use the FPTRAPI
# instrumentation interface
#
CUSTOMALGS = AdapTM
