CC     := @CMAKE_C_COMPILER@
CXX    := @CMAKE_CXX_COMPILER@
TMLINK := tmlink
VPATH  := @CMAKE_CURRENT_SOURCE_DIR@:@CMAKE_CURRENT_SOURCE_DIR@/../lib

STMLIB := @CMAKE_CURRENT_BINARY_DIR@/../../libitm2stm
STMSUPPORT := $(dir $(shell which ${TMLINK}))../lib

CFLAGS   = -I@CMAKE_CURRENT_SOURCE_DIR@/../lib
CFLAGS  += -DLIST_NO_DUPLICATES -DMAP_USE_RBTREE -DDTMC
CFLAGS  += -fgnu-tm -emit-llvm
CFLAGS  += -O3

CXXFLAGS := ${CFLAGS} #-fno-exceptions

LDFLAGS  = -stmlib=${STMLIB}
LDFLAGS += -tm-support-file=${STMLIB}/libtanger-stm.support
LDFLAGS += -stmsupport=${STMSUPPORT}
LDFLAGS += -tanger-add-shutdown-call
LDFLAGS += -tanger-whole-program
LDFLAGS += -tanger-indirect-auto
LDFLAGS += -sandboxpass=sandbox-tm

ifdef NATIVE
LDFLAGS += -n
endif

TANGERFLAGS  = -tanger
TANGERFLAGS += -tanger-whole-program
TANGERFLAGS += -tanger-indirect-auto
TANGERFLAGS += -internalize-public-api-file=${STMLIB}/libtanger-stm.public-symbols

LDLIBS  := -lrt -ldl

all: vacation

clean:
	@find . -name "*.bc" | xargs rm -f
	@find . -name "*.ll" | xargs rm -f
	@find . -name "*.o" | xargs rm -f
	@find . -name "vacation" | xargs rm -f

vacation: pair.bc mt19937ar.bc random.bc thread.bc client.bc customer.bc \
          manager.bc reservation.bc vacation.bc list.bc rbtree.bc
	${TMLINK} ${LDFLAGS} -o $@ $^ ${LDLIBS}

%.bc: %.c
	${CC} ${CFLAGS} -o $@ -c $<

%.bc: %.cpp
	${CXX} ${CXXFLAGS} -o $@ -c $<

BITS   ?= 64
TRIALS ?= 3
CORES  ?= 12
ALGS   ?= CGL OrecEager OrecELA OrecSandbox

ifdef BIND
CPUSET = 1,3,5,7,9,11,13,15,17,19,21,23
else
CPUSET = 1,3,5,7,9,11,13,15,17,19,21,23,0,2,4,6,8,10,12,14,16,18,20,22
endif

vacation.high: vacation
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

vacation.low: vacation
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

vacation.sandbox.bc: vacation.tanger.bc
	opt -load /u/luked/proj/tanger-2.9.rc1-sandboxing-obj/Debug+Asserts/lib/libtanger.so -sandbox-tm -debug-only=sandbox -o $@ $^

vacation.tanger.bc: pair.bc mt19937ar.bc random.bc thread.bc client.bc \
	                customer.bc manager.bc reservation.bc vacation.bc list.bc \
                    rbtree.bc
	llvm-ld -load /u/luked/proj/tanger-2.9.rc1-sandboxing-obj/Debug+Asserts/lib/libtanger.so -link-as-library ${TANGERFLAGS} -mem2reg -o $@ $^
