# -*- Makefile -*-*
#
# define EXECS and VPATH, and then include the support files.
EXECS    := yada
VPATH     = @CMAKE_CURRENT_SOURCE_DIR@
include ../../sandbox.common.mk
include ../sandbox.stamp.mk
CFLAGS   += -DLIST_NO_DUPLICATES -DMAP_USE_AVLTREE -DSET_USE_RBTREE
CXXFLAGS += -DLIST_NO_DUPLICATES -DMAP_USE_AVLTREE -DSET_USE_RBTREE

yada: avltree.bc heap.bc list.bc mt19937ar.bc pair.bc queue.bc random.bc \
      rbtree.bc thread.bc vector.bc \
      coordinate.bc element.bc mesh.bc region.bc yada.bc

test.cgl: yada
	for trials in {1..${TRIALS}}; \
	do \
		echo "STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -R -p1"; \
		STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -t1 -a15 -i @CMAKE_CURRENT_SOURCE_DIR@/inputs/ttimeu1000000.2; \
	done

test: yada test.cgl
	for stm in ${ALGS}; \
	do \
		for trials in {1..${TRIALS}}; \
		do \
			for i in {1..${CORES}}; \
			do \
				echo "STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -R -p$$i"; \
				STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -t$$i -a15 -i @CMAKE_CURRENT_SOURCE_DIR@/inputs/ttimeu1000000.2; \
			done \
		done \
	done
