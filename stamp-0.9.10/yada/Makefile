##################################################
# Begin declarations specific to this folder
##################################################

# specify the names of the files in this folder that need to be compiled.  Be
# sure to just provide the filename, with no extension
PROG_SOURCES = coordinate element mesh region yada

# specify the names of the files in the lib folder that need to be compiled.
# Be sure to just provide the filename, with no path or extension
LIB_SOURCES  = avltree heap list mt19937ar pair queue random rbtree \
               thread vector

# specify the name of the executable to produce
PROGNAME     = yada

# specify any custom build flags
CUSTOM_CXXFLAGS = -DLIST_NO_DUPLICATES -DMAP_USE_AVLTREE -DSET_USE_RBTREE

##################################################
# End of folder-specific content
##################################################

include ../Makefile.common
