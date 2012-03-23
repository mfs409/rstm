# -*- Makefile -*-*
#
# define EXECS and VPATH, and then include the support files.
EXECS    := labyrinth
VPATH     = @CMAKE_CURRENT_SOURCE_DIR@
include ../../sandbox.common.mk
include ../sandbox.stamp.mk

labyrinth: list.bc mt19937ar.bc pair.bc queue.bc random.bc thread.bc \
           vector.bc \
           coordinate.bc grid.bc labyrinth.bc maze.bc router.bc

test.cgl: labyrinth
	for trials in {1..${TRIALS}}; \
	do \
		echo "STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -R -p1"; \
		STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -t1 -i @CMAKE_CURRENT_SOURCE_DIR@/inputs/random-x512-y512-z7-n512.txt; \
	done

test: labyrinth test.cgl
	for stm in ${ALGS}; \
	do \
		for trials in {1..${TRIALS}}; \
		do \
			for i in {1..${CORES}}; \
			do \
				echo "STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -R -p$$i"; \
				STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -t$$i -i @CMAKE_CURRENT_SOURCE_DIR@/inputs/random-x512-y512-z7-n512.txt; \
			done \
		done \
	done
