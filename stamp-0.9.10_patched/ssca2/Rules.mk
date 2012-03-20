#CFLAGS += -DUSE_PARALLEL_DATA_GENERATION
#CFLAGS += -DWRITE_RESULT_FILES
CFLAGS += -DENABLE_KERNEL1
#CFLAGS += -DENABLE_KERNEL2 -DENABLE_KERNEL3
#CFLAGS += -DENABLE_KERNEL4

PROG       = ssca2
BENCHNAMES = alg_radix_smp computeGraph createPartition cutClusters	\
             findSubGraphs genScalData getStartLists getUserParameters	\
             globals ssca2
LIBNAMES   = mt19937ar random thread
