# -*- Makefile -*-*
EXECS   = HashBench TreeBench ListBench
VPATH   = @CMAKE_CURRENT_SOURCE_DIR@
include ../sandbox.common.mk
CXXFLAGS += -I@CMAKE_SOURCE_DIR@ -fno-rtti -fno-exceptions

# squash the complaint about tm_pure
bmharness.o: CXXFLAGS := $(CXXFLAGS) -Wno-attributes

HashBench: HashBench.bc bmharness.o

TreeBench: TreeBench.bc bmharness.o

ListBench: ListBench.bc bmharness.o

TIME ?= 5
RRS  ?= 100 75 50 25 0

%.cgl-test.set: %
	for trials in {1..${TRIALS}}; \
	do \
		for r in ${RRS}; \
		do \
			echo "STM_CONFIG=CGL taskset -c ${CPUSET} ./$^ -R$$r -p1"; \
			STM_CONFIG=CGL taskset -c ${CPUSET} ./$^ -R$$r -d${TIME} -p1; \
		done \
	done

%.parallel-test.set: %
	for stm in ${ALGS}; \
	do \
		for trials in {1..${TRIALS}}; \
		do \
			for i in {1..${CORES}}; \
			do \
				for r in ${RRS}; \
				do \
					echo "STM_CONFIG=$$stm taskset -c ${CPUSET} ./$^ -R$$r -p$$i"; \
					STM_CONFIG=$$stm taskset -c ${CPUSET} ./$^ -R$$r -d${TIME} -p$$i; \
				done \
			done \
		done \
	done

hash-test: HashBench.cgl-test.set HashBench.parallel-test.set

list-test: ListBench.cgl-test.set ListBench.parallel-test.set

tree-test: TreeBench.cgl-test.set TreeBench.parallel-test.set

test: hash-test list-test tree-test email
