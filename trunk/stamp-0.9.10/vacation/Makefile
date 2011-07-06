##################################################
# Begin declarations specific to this folder
##################################################

# specify the names of the files in this folder that need to be compiled.  Be
# sure to just provide the filename, with no extension
PROG_SOURCES = client customer manager reservation vacation

# specify the names of the files in the lib folder that need to be compiled.
# Be sure to just provide the filename, with no path or extension
LIB_SOURCES  = list pair mt19937ar random rbtree thread

# specify the name of the executable to produce
PROGNAME     = vacation

# specify any custom build flags
CUSTOM_CXXFLAGS = -DLIST_NO_DUPLICATES -DMAP_USE_RBTREE

##################################################
# End of folder-specific content
##################################################

include ../Makefile.common
