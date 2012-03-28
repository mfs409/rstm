# -*- Makefile -*-*
#
# define EXECS and VPATH, and then include the support files.
EXECS    := vacation
VPATH     = @CMAKE_CURRENT_SOURCE_DIR@
include ../../sandbox.common.mk
include ../sandbox.stamp.mk
CFLAGS   += -DLIST_NO_DUPLICATES -DMAP_USE_RBTREE
CXXFLAGS += -DLIST_NO_DUPLICATES -DMAP_USE_RBTREE

vacation: pair.bc mt19937ar.bc random.bc thread.bc client.bc customer.bc \
          manager.bc reservation.bc vacation.bc list.bc rbtree.bc

vacation.high.cgl: vacation
	for trials in {1..${TRIALS}}; \
	do \
		echo "STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -Rhigh -p1"; \
		STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -n4 -q60 -u90 -r1048576 -t4194304 -c1; \
	done

vacation.low.cgl: vacation
	for trials in {1..${TRIALS}}; \
	do \
		echo "STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -Rlow -p1"; \
		STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -n2 -q90 -u98 -r1048576 -t4194304 -c1; \
	done

vacation.high: vacation vacation.high.cgl
	for stm in ${ALGS}; \
	do \
		for trials in {1..${TRIALS}}; \
		do \
			for i in {1..${CORES}}; \
			do \
				echo "STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -Rhigh -p$$i"; \
				STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -n4 -q60 -u90 -r1048576 -t4194304 -c$$i; \
			done \
		done \
	done

vacation.low: vacation vacation.low.cgl
	for stm in ${ALGS}; \
	do \
		for trials in {1..${TRIALS}}; \
		do \
			for i in {1..${CORES}}; \
			do \
				echo "STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -Rlow -p$$i"; \
				STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -n2 -q90 -u98 -r1048576 -t4194304 -c$$i; \
			done \
		done \
	done

test: vacation.high vacation.low
