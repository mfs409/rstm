#
# For lack of a better place to put it, we'll declare the different APIs that
# a benchmark can target in this Makefile
#

APIS = lockapi genericapi

#
# The names of the STM algorithms, and of all supporting compilable files
#
ALGNAMES    = cgl norec tml cohortseager cohorts ctokenturbo ctoken   \
              llt oreceagerredo orecela orecala oreclazy oreceager
