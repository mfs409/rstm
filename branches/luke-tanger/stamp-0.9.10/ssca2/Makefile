TORULES  := ../../..
SOURCES  := ../lib/mt19937ar.c ../lib/random.c ../lib/thread.c \
            alg_radix_smp.c computeGraph.c createPartition.c cutClusters.c \
            findSubGraphs.c genScalData.c getStartLists.c getUserParameters.c \
            globals.c ssca2.c
CFLAGS   += -I../lib -I../.. -I../../include -I$(TORULES)/../build/rstm/include
CFLAGS   += -DENABLE_KERNEL1
    #USE_PARALLEL_DATA_GENERATION
    #WRITE_RESULT_FILES
    #ENABLE_KERNEL2
    #ENABLE_KERNEL3
    #ENABLE_KERNEL4
LLVM-GCC := llvm-g++
EXECS    := ssca2

-include $(TORULES)/Makefile.rules

test: ssca2
	@echo "sandboxing test run: ssca2"
	./ssca2 -s20 -i1.0 -u1.0 -l3 -p3