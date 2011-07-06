##################################################
# Begin declarations specific to this folder
##################################################

# specify the names of the files in this folder that need to be compiled.  Be
# sure to just provide the filename, with no extension
PROG_SOURCES = adtree bayes data learner net sort

# specify the names of the files in the lib folder that need to be compiled.
# Be sure to just provide the filename, with no path or extension
LIB_SOURCES  = bitmap list mt19937ar queue random thread vector

# specify the name of the executable to produce
PROGNAME     = bayes

# specify any custom build flags
CUSTOM_CXXFLAGS = -DLIST_NO_DUPLICATES -DLEARNER_TRY_REMOVE \
                  -DLEARNER_TRY_REVERSE

##################################################
# End of folder-specific content
##################################################

include ../Makefile.common
