# -*- Makefile -*-*
CC.o   := gcc
CXX.o  := g++
CXX.ld := g++ -Wl,-plugin,/u/luked/pub/gcc/4.8/lib64/bfd-plugins/LLVMgold.so
CC.bc  := @CMAKE_C_COMPILER@ -emit-llvm -fgnu-tm
CXX.bc := @CMAKE_CXX_COMPILER@ -emit-llvm -fgnu-tm
TMLINK := tmlink
VPATH  := @CMAKE_CURRENT_SOURCE_DIR@:@CMAKE_CURRENT_SOURCE_DIR@/../lib

STMLIB := @CMAKE_CURRENT_BINARY_DIR@/../../libitm2stm
STMSUPPORT := $(dir $(shell which ${TMLINK}))../lib

CFLAGS   = -I@CMAKE_CURRENT_SOURCE_DIR@/../lib
CFLAGS  += -DLIST_NO_DUPLICATES -DMAP_USE_RBTREE -DDTMC -DSTM_API_DTMC

CXXFLAGS := ${CFLAGS} # -fno-exceptions

TMLINKFLAGS  = -stmlib=${STMLIB}
TMLINKFLAGS += -tm-support-file=${STMLIB}/libtanger-stm.support
TMLINKFLAGS += -stmsupport=${STMSUPPORT}
TMLINKFLAGS += -tanger-add-shutdown-call
TMLINKFLAGS += -tanger-whole-program
TMLINKFLAGS += -tanger-indirect-auto
TMLINKFLAGS += -sandboxpass=sandbox-tm

OPTFLAGS  = -load $(STMSUPPORT)/libtanger.so
OPTFLAGS += -link-as-library
OPTFLAGS += -tanger
OPTFLAGS += -tanger-whole-program
OPTFLAGS += -tanger-indirect-auto
OPTFLAGS += -tanger-add-shutdown-call
OPTFLAGS += -mem2reg
OPTFLAGS += -sandbox-tm

OPT_BC ?= -O3
OPT_O  ?= -O3

ifdef NATIVE
LDFLAGS = -L$(STMLIB)
LDLIBS  = -litm
else
LDLIBS  = $(STMLIB)/libtanger-stm.bc $(STMLIB)/libtanger-stm.a
endif

LDFLAGS += -pthread
LDLIBS  += -ldl -lrt

all: vacation

clean:
	@find . -name "*.bc" | xargs rm -f
	@find . -name "*.ll" | xargs rm -f
	@find . -name "*.o" | xargs rm -f
	@find . -name "vacation" | xargs rm -f

vacation: pair.bc mt19937ar.bc random.bc thread.bc client.bc customer.bc \
          manager.bc reservation.bc vacation.bc list.bc rbtree.bc
	llvm-ld $(OPTFLAGS) -o $@.tx.bc $(filter %.bc,$^)
	$(CXX.ld) $(LDFLAGS) $(OPT_O) -o $@ $@.tx.bc $(STMSUPPORT)/stmsupport.bc $(filter-out %.bc,$^) $(LDLIBS)

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
CORES  ?= 12
ALGS   ?= OrecELA OrecSandbox

ifdef BIND
CPUSET = 1,3,5,7,9,11,13,15,17,19,21,23
else
CPUSET = 1,3,5,7,9,11,13,15,17,19,21,23,0,2,4,6,8,10,12,14,16,18,20,22
endif

vacation.high.cgl: vacation
	for trials in {1..${TRIALS}}; \
	do \
		echo "STM_CONFIG=CGL taskset -c ${CPUSET} ./$^ -Rhigh -p1"; \
		STM_CONFIG=CGL taskset -c ${CPUSET} ./$^ -n4 -q60 -u90 -r1048576 -t4194304 -c1; \
	done

vacation.low.cgl: vacation
	for trials in {1..${TRIALS}}; \
	do \
		echo "STM_CONFIG=CFG taskset -c ${CPUSET} ./$^ -Rlow -p1"; \
		STM_CONFIG=CFG taskset -c ${CPUSET} ./$^ -n2 -q90 -u98 -r1048576 -t4194304 -c1; \
	done

vacation.high: vacation vacation.high.cgl
	for stm in ${ALGS}; \
	do \
		for trials in {1..${TRIALS}}; \
		do \
			for i in {1..${CORES}}; \
			do \
				echo "STM_CONFIG=$$stm taskset -c ${CPUSET} ./$^ -Rhigh -p$$i"; \
				STM_CONFIG=$$stm taskset -c ${CPUSET} ./$^ -n4 -q60 -u90 -r1048576 -t4194304 -c$$i; \
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
				echo "STM_CONFIG=$$stm taskset -c ${CPUSET} ./$^ -Rlow -p$$i"; \
				STM_CONFIG=$$stm taskset -c ${CPUSET} ./$^ -n2 -q90 -u98 -r1048576 -t4194304 -c$$i; \
			done \
		done \
	done

test: vacation.high vacation.low
