# -*- Makefile -*-*
#
# define EXECS and VPATH, and then include the support files.
EXECS    := kmeans
VPATH     = @CMAKE_CURRENT_SOURCE_DIR@
include ../../sandbox.common.mk
include ../sandbox.stamp.mk
CFLAGS   += -DOUTPUT_TO_STDOUT
CXXFLAGS += -DOUTPUT_TO_STDOUT

kmeans: mt19937ar.bc random.bc thread.bc \
        cluster.bc common.bc kmeans.bc normal.bc

kmeans.low.cgl: kmeans
	for trials in {1..${TRIALS}}; \
	do \
		echo "STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -Rlow -p1"; \
		STM_CONFIG=CGL taskset -c ${CPUSET} ./$<  -m40 -n40 -t0.00001 -p1 -i @CMAKE_CURRENT_SOURCE_DIR@/inputs/random-n65536-d32-c16.txt; \
	done

kmeans.low: kmeans kmeans.low.cgl
	for stm in ${ALGS}; \
	do \
		for trials in {1..${TRIALS}}; \
		do \
			for i in {1..${CORES}}; \
			do \
				echo "STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -Rlow -p$$i"; \
				STM_CONFIG=$$stm taskset -c ${CPUSET} ./$<  -m40 -n40 -t0.00001 -p$$i -i @CMAKE_CURRENT_SOURCE_DIR@/inputs/random-n65536-d32-c16.txt; \
			done \
		done \
	done


kmeans.high.cgl: kmeans
	for trials in {1..${TRIALS}}; \
	do \
		echo "STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -Rhigh -p1"; \
		STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -m15 -n15 -t0.00001 -p1 -i @CMAKE_CURRENT_SOURCE_DIR@/inputs/random-n65536-d32-c16.txt; \
	done

kmeans.high: kmeans kmeans.high.cgl
	for stm in ${ALGS}; \
	do \
		for trials in {1..${TRIALS}}; \
		do \
			for i in {1..${CORES}}; \
			do \
				echo "STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -Rhigh -p$$i"; \
				STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -m15 -n15 -t0.00001 -p$$i -i @CMAKE_CURRENT_SOURCE_DIR@/inputs/random-n65536-d32-c16.txt; \
			done \
		done \
	done

test: kmeans.high kmeans.low