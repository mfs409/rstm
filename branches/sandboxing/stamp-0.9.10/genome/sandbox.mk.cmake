# -*- Makefile -*-*
#
# define EXECS and VPATH, and then include the support files.
EXECS    := genome
VPATH     = @CMAKE_CURRENT_SOURCE_DIR@
include ../../sandbox.common.mk
include ../sandbox.stamp.mk
CFLAGS   += -DLIST_NO_DUPLICATES -DCHUNK_STEP1=12
CXXFLAGS += -DLIST_NO_DUPLICATES -DCHUNK_STEP1=12

genome: bitmap.bc hash.bc hashtable.bc pair.bc random.bc list.bc mt19937ar.bc \
        thread.bc vector.bc \
        gene.bc genome.bc segments.bc sequencer.bc table.bc

test.cgl: genome
	for trials in {1..${TRIALS}}; \
	do \
		echo "STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -R -p1"; \
		STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -g16384 -s64 -n16777216 -t1; \
	done

test: genome test.cgl
	for stm in ${ALGS}; \
	do \
		for trials in {1..${TRIALS}}; \
		do \
			for i in {1..${CORES}}; \
			do \
				echo "STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -R -p$$i"; \
				STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -g16384 -s64 -n16777216 -t$$i; \
			done \
		done \
	done
