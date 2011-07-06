##################################################
# Begin declarations specific to this folder
##################################################

# specify the names of the files in this folder that need to be compiled.  Be
# sure to just provide the filename, with no extension
PROG_SOURCES = gene genome segments sequencer table

# specify the names of the files in the lib folder that need to be compiled.
# Be sure to just provide the filename, with no path or extension
LIB_SOURCES  = bitmap hash hashtable pair random list mt19937ar thread vector

# specify the name of the executable to produce
PROGNAME     = genome

# specify any custom build flags
CUSTOM_CXXFLAGS = -DLIST_NO_DUPLICATES -DCHUNK_STEP1=12

##################################################
# End of folder-specific content
##################################################

include ../Makefile.common
