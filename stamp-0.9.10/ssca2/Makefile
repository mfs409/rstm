##################################################
# Begin declarations specific to this folder
##################################################

# specify the names of the files in this folder that need to be compiled.  Be
# sure to just provide the filename, with no extension
PROG_SOURCES = alg_radix_smp computeGraph createPartition cutClusters    \
               findSubGraphs genScalData getStartLists getUserParameters \
               globals ssca2

# specify the names of the files in the lib folder that need to be compiled.
# Be sure to just provide the filename, with no path or extension
LIB_SOURCES  = mt19937ar random thread

# specify the name of the executable to produce
PROGNAME     = ssca2

# specify any custom build flags
CUSTOM_CXXFLAGS = -DENABLE_KERNEL1
#CUSTOM_CXXFLAGS = -DUSE_PARALLEL_DATA_GENERATION
#CUSTOM_CXXFLAGS = -DWRITE_RESULT_FILES
#CUSTOM_CXXFLAGS = -DENABLE_KERNEL2 -DENABLE_KERNEL3
#CUSTOM_CXXFLAGS = -DENABLE_KERNEL4

##################################################
# End of folder-specific content
##################################################

include ../Makefile.common
