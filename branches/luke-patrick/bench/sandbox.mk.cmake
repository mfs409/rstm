# -*- Makefile -*-*
CC.o   := gcc
CXX.o  := g++
CC.bc  := @CMAKE_C_COMPILER@ -fgnu-tm -emit-llvm
CXX.bc := @CMAKE_CXX_COMPILER@ -fgnu-tm -emit-llvm
TMLINK := tmlink
VPATH  := @CMAKE_CURRENT_SOURCE_DIR@

STMLIB := @CMAKE_CURRENT_BINARY_DIR@/../libitm2stm
STMSUPPORT := $(dir $(shell which ${TMLINK}))../lib

CFLAGS   = -I@CMAKE_SOURCE_DIR@ -I@CMAKE_SOURCE_DIR@/include -I@CMAKE_BINARY_DIR@/include 
CFLAGS  += -DSTM_API_DTMC

ifdef DEBUG
CFLAGS  += -O0 -g
else
CFLAGS  += -O3
endif

CXXFLAGS := ${CFLAGS} #-fno-exceptions

LDFLAGS  = -stmlib=${STMLIB}
LDFLAGS += -tm-support-file=${STMLIB}/libtanger-stm.support
LDFLAGS += -stmsupport=${STMSUPPORT}
LDFLAGS += -tanger-add-shutdown-call
LDFLAGS += -sandboxpass=sandbox-tm

ifdef NATIVE
LDFLAGS += -n
endif

ifdef DEBUG
LDFLAGS += -disable-internalize
endif

LDLIBS  += -ldl -lrt

all: HashBench TreeBench ListBench

clean:
	@find . -name "*.bc" | xargs rm -f
	@find . -name "*.ll" | xargs rm -f
	@find . -name "*.o" | xargs rm -f
	@find . -name "HashBench" | xargs rm -f
	@find . -name "TreeBench" | xargs rm -f
	@find . -name "ListBench" | xargs rm -f

HashBench: HashBench.bc bmharness.bc
	${TMLINK} ${LDFLAGS} -o $@ $^ ${LDLIBS}

TreeBench: TreeBench.bc bmharness.bc
	${TMLINK} ${LDFLAGS} -o $@ $^ ${LDLIBS}

ListBench: ListBench.bc bmharness.bc
	${TMLINK} ${LDFLAGS} -o $@ $^ ${LDLIBS}

%.bc: %.c
	${CC.bc} ${CFLAGS} -o $@ -c $<

%.bc: %.cpp
	${CXX.bc} ${CXXFLAGS} -o $@ -c $<

%.o: %.c
	${CC.o} ${CFLAGS} -g -o $@ -c $<

%.o: %.cpp
	${CXX.o} ${CXXFLAGS} -g -o $@ -c $<


BITS   ?= 64
TRIALS ?= 3
CORES  ?= 6
TIME   ?= 5
ALGS   ?= OrecEager OrecELA
#OrecSandbox

ifdef BIND
CPUSET = 1,3,5,7,9,11,13,15,17,19,21,23
else
CPUSET = 1,3,5,7,9,11,13,15,17,19,21,23,0,2,4,6,8,10,12,14,16,18,20,22
endif

RRS    := 100 80 50 34 0

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

test: HashBench.cgl-test.set \
               TreeBench.cgl-test.set \
               ListBench.cgl-test.set \
			   HashBench.parallel-test.set \
               TreeBench.parallel-test.set \
               ListBench.parallel-test.set
