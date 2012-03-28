# -*- Makefile -*-*
#
# define EXECS and VPATH, and then include the support files.
EXECS    := ssca2
VPATH     = @CMAKE_CURRENT_SOURCE_DIR@
include ../../sandbox.common.mk
include ../sandbox.stamp.mk
CFLAGS   += -DENABLE_KERNEL1
CXXFLAGS += -DENABLE_KERNEL1

ssca2: mt19937ar.bc random.bc thread.bc \
       alg_radix_smp.bc computeGraph.bc createPartition.bc cutClusters.bc \
       findSubGraphs.bc genScalData.bc getStartLists.bc getUserParameters.bc \
       globals.bc ssca2.bc

test.cgl: ssca2
	for trials in {1..${TRIALS}}; \
	do \
		echo "STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -R -p1"; \
		STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -s20 -i1.0 -u1.0 -l3 -p3 -t1; \
	done

test: ssca2 test.cgl
	for stm in ${ALGS}; \
	do \
		for trials in {1..${TRIALS}}; \
		do \
			for i in {1..${CORES}}; \
			do \
				echo "STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -R -p$$i"; \
				STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -s20 -i1.0 -u1.0 -l3 -p3 -t$$i; \
			done \
		done \
	done
