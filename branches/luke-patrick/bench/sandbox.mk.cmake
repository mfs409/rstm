# -*- Makefile -*-*
CC     := @CMAKE_C_COMPILER@
CXX    := @CMAKE_CXX_COMPILER@
TMLINK := tmlink
VPATH  := @CMAKE_CURRENT_SOURCE_DIR@

STMLIB := @CMAKE_CURRENT_BINARY_DIR@/../libitm2stm
STMSUPPORT := $(dir $(shell which ${TMLINK}))../lib

CFLAGS   = -I@CMAKE_SOURCE_DIR@ -I@CMAKE_SOURCE_DIR@/include -I@CMAKE_BINARY_DIR@/include 
CFLAGS  += -DSTM_API_DTMC -DSINGLE_SOURCE_BUILD
CFLAGS  += -fgnu-tm -emit-llvm

ifndef DEBUG
CFLAGS  += -O3
endif

CXXFLAGS := ${CFLAGS} #-fno-exceptions

LDFLAGS  = -stmlib=${STMLIB}
LDFLAGS += -stmsupport=${STMSUPPORT}
LDFLAGS += -tm-support-file=${STMLIB}/libtanger-stm.support
LDFLAGS += -internalize-public-api-file=${STMLIB}/libtanger-stm.public-symbols
LDFLAGS += -sandboxpass=sandbox-tm

ifdef DEBUG
LDFLAGS += -disable-opt
endif

LDLIBS = -ldl -lrt

all: HashBench TreeBench ListBench

clean:
	@find . -name "*.bc" | xargs rm -f
	@find . -name "*.ll" | xargs rm -f
	@find . -name "*.o" | xargs rm -f
	@find . -name "HashBench" | xargs rm -f
	@find . -name "TreeBench" | xargs rm -f
	@find . -name "ListBench" | xargs rm -f

HashBench: HashBench.bc
	${TMLINK} ${LDFLAGS} -o $@ $^ ${LDLIBS}

TreeBench: TreeBench.bc
	${TMLINK} ${LDFLAGS} -o $@ $^ ${LDLIBS}

ListBench: ListBench.bc
	${TMLINK} ${LDFLAGS} -o $@ $^ ${LDLIBS}

%.bc: %.c
	${CC} ${CFLAGS} -o $@ -c $<

%.bc: %.cpp
	${CXX} ${CXXFLAGS} -o $@ -c $<

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
