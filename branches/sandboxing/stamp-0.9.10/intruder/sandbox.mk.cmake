# -*- Makefile -*-*
#
# define EXECS and VPATH, and then include the support files.
EXECS    := intruder
VPATH     = @CMAKE_CURRENT_SOURCE_DIR@
include ../../sandbox.common.mk
include ../sandbox.stamp.mk
CFLAGS   += -DMAP_USE_RBTREE
CXXFLAGS += -DMAP_USE_RBTREE

intruder: list.bc mt19937ar.bc pair.bc queue.bc random.bc rbtree.bc thread.bc \
          vector.bc \
          decoder.bc detector.bc dictionary.bc intruder.bc packet.bc \
          preprocessor.bc stream.bc

test.cgl: intruder
	for trials in {1..${TRIALS}}; \
	do \
		echo "STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -R -p1"; \
		STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -a10 -l128 -n262144 -s1 -t1; \
	done

test: intruder test.cgl
	for stm in ${ALGS}; \
	do \
		for trials in {1..${TRIALS}}; \
		do \
			for i in {1..${CORES}}; \
			do \
				echo "STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -R -p$$i"; \
				STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -a10 -l128 -n262144 -s1 -t$$i; \
			done \
		done \
	done
