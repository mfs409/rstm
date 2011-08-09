#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
#
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information

option(
  itm2stm_enable_assert_on_irrevocable
  "ON causes calls to _ITM_changeTransactionMode to fail (useful for debuggin)"
  OFF)