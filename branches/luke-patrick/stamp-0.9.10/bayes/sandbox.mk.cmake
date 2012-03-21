# -*- Makefile -*-*
#
# define EXECS and VPATH, and then include the support files.
EXECS    := bayes
VPATH     = @CMAKE_CURRENT_SOURCE_DIR@
include ../../sandbox.common.mk
include ../sandbox.stamp.mk
CFLAGS   += -DLIST_NO_DUPLICATES -DLEARNER_TRY_REMOVE -DLEARNER_TRY_REVERSE
CXXFLAGS += -DLIST_NO_DUPLICATES -DLEARNER_TRY_REMOVE -DLEARNER_TRY_REVERSE

bayes: bitmap.bc list.bc mt19937ar.bc queue.bc random.bc thread.bc vector.bc \
       adtree.bc bayes.bc data.bc learner.bc net.bc sort.bc

test.cgl: bayes
	for trials in {1..${TRIALS}}; \
	do \
		echo "STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -R -p1"; \
		STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -v32 -r4096 -n10 -p40 -i2 -e8 -s1 -t1; \
	done

test: bayes test.cgl
	for stm in ${ALGS}; \
	do \
		for trials in {1..${TRIALS}}; \
		do \
			for i in {1..${CORES}}; \
			do \
				echo "STM_CONFIG=$$stm taskset -c ${CPUSET} ./$< -R -p$$i"; \
				STM_CONFIG=CGL taskset -c ${CPUSET} ./$< -v32 -r4096 -n10 -p40 -i2 -e8 -s1 -t$$i; \
			done \
		done \
	done
