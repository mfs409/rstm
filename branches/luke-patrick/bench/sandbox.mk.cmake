# -*- Makefile -*-*
CC.o   := gcc
CXX.o  := g++
CC.bc  := @CMAKE_C_COMPILER@ -emit-llvm
CXX.bc := @CMAKE_CXX_COMPILER@ -emit-llvm
TMLINK := tmlink
VPATH  := @CMAKE_CURRENT_SOURCE_DIR@

STMLIB := @CMAKE_CURRENT_BINARY_DIR@/../libitm2stm
STMSUPPORT := $(dir $(shell which ${TMLINK}))../lib

CFLAGS   = -I@CMAKE_SOURCE_DIR@ -I@CMAKE_SOURCE_DIR@/include -I@CMAKE_BINARY_DIR@/include 
CFLAGS  += -DSTM_API_TANGER -fno-exceptions -fno-rtti

CXXFLAGS := ${CFLAGS} #-fno-exceptions

LDFLAGS  = -stmlib=${STMLIB}
LDFLAGS += -tm-support-file=${STMLIB}/libtanger-stm.support
LDFLAGS += -stmsupport=${STMSUPPORT}
LDFLAGS += -tanger-add-shutdown-call
LDFLAGS += -tanger-whole-program
LDFLAGS += -tanger-indirect-auto
LDFLAGS += -sandboxpass=sandbox-tm

OPTFLAGS  = -load $(STMSUPPORT)/libtanger.so
OPTFLAGS += -tanger
OPTFLAGS += -tanger-whole-program
OPTFLAGS += -tanger-indirect-auto
OPTFLAGS += -tanger-add-shutdown-call
OPTFLAGS += -mem2reg
OPTFLAGS += -sandbox-tm

ifdef NATIVE
LDFLAGS += -n
endif

OPT_BC ?= -O3
OPT_O  ?= -O3

LDLIBS  += -ldl -lrt

all: HashBench TreeBench ListBench

clean:
	@find . -name "*.bc" | xargs rm -f
	@find . -name "*.ll" | xargs rm -f
	@find . -name "*.o" | xargs rm -f
	@find . -name "HashBench" | xargs rm -f
	@find . -name "TreeBench" | xargs rm -f
	@find . -name "ListBench" | xargs rm -f

HashBench: HashBench.bc bmharness.o
	opt $(OPTFLAGS) -o $@.tx.bc $(filter %.bc,$^)
	$(CXX.o) -O3 -flto -Wl,-plugin,/u/luked/pub/gcc/4.8/lib64/bfd-plugins/LLVMgold.so -L$(STMLIB) -pthread -o $@ $@.tx.bc $(STMSUPPORT)/stmsupport.bc $(filter-out %.bc,$^) -litm $(LDLIBS)

# ${TMLINK} ${LDFLAGS} -o $@ $^ ${LDLIBS}

TreeBench: TreeBench.bc bmharness.o
	opt $(OPTFLAGS) -o $@.tx.bc $(filter %.bc,$^)
	$(CXX.o) -O3 -flto -Wl,-plugin,/u/luked/pub/gcc/4.8/lib64/bfd-plugins/LLVMgold.so -L$(STMLIB) -pthread -o $@ $@.tx.bc $(STMSUPPORT)/stmsupport.bc $(filter-out %.bc,$^) -litm $(LDLIBS)

ListBench: ListBench.bc bmharness.o
	opt $(OPTFLAGS) -o $@.tx.bc $(filter %.bc,$^)
	$(CXX.o) -O3 -flto -Wl,-plugin,/u/luked/pub/gcc/4.8/lib64/bfd-plugins/LLVMgold.so -L$(STMLIB) -pthread -o $@ $@.tx.bc $(STMSUPPORT)/stmsupport.bc $(filter-out %.bc,$^) -litm $(LDLIBS)


%.bc: %.c
	${CC.bc} ${CFLAGS} $(OPT_BC) -o $@ -c $<

%.bc: %.cpp
	${CXX.bc} ${CXXFLAGS} $(OPT_BC) -o $@ -c $<

%.o: %.c
	${CC.o} ${CFLAGS} $(OPT_O) -o $@ -c $<

%.o: %.cpp
	${CXX.o} ${CXXFLAGS} $(OPT_O) -o $@ -c $<

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
