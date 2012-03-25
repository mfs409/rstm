# -*- Makefile -*-*
# Define EXECS before including this file.
CC.o    := gcc
CXX.o   := g++
CXX.ld  := g++ -Wl,-plugin,`$(CXX.o) -print-file-name=LLVMgold.so`
CC.bc   := @CMAKE_C_COMPILER@ -emit-llvm
CXX.bc  := @CMAKE_CXX_COMPILER@ -emit-llvm
LD.llvm := llvm-ld

STMLIB := @CMAKE_BINARY_DIR@/libitm2stm
STMSUPPORT := $(dir $(shell which tmlink))../lib

ifdef DTMC
CFLAGS    = -DDTMC -DSTM_API_DTMC -fgnu-tm
CXXFLAGS  = -DDTMC -DSTM_API_DTMC -fgnu-tm
else
CFLAGS    = -DTANGER -DSTM_API_TANGER
CXXFLAGS  = -DTANGER -DSTM_API_TANGER
endif

CFLAGS   += -I@CMAKE_SOURCE_DIR@/include -I@CMAKE_BINARY_DIR@/include
CXXFLAGS += -I@CMAKE_SOURCE_DIR@/include -I@CMAKE_BINARY_DIR@/include

LDFLAGS.llvm  = -load $(STMSUPPORT)/libtanger.so
LDFLAGS.llvm += -link-as-library
LDFLAGS.llvm += -tanger
LDFLAGS.llvm += -tanger-whole-program
LDFLAGS.llvm += -tanger-indirect-auto
LDFLAGS.llvm += -tanger-add-shutdown-call
LDFLAGS.llvm += -mem2reg
LDFLAGS.llvm += -sandbox-tm

ifdef V
LDFLAGS.llvm += -debug-only=sandbox
LDFLAGS.llvm += -stats
LDFLAGS.llvm += -v
endif

ifdef DEBUG
LDFLAGS.llvm += -disable-opt
CFLAGS.bc     = -O0
CFLAGS.o      = -O0 -g
CXXFLAGS.bc   = -O0
CXXFLAGS.o    = -O0 -g
LDFLAGS.o     = -O0
else
CFLAGS.bc     = -O3 
CFLAGS.o      = -O3
CXXFLAGS.bc   = -O3 
CXXFLAGS.o    = -O3
LDFLAGS.o     = -O3
endif

ifdef NATIVE
LDFLAGS.o    += -L$(STMLIB)
LDLIBS        = -litm
else
LDLIBS        = $(STMLIB)/libtanger-stm.bc $(STMLIB)/libtanger-stm.a
endif

ifdef PROF
CFLAGS.bc     += -pg
CFLAGS.o      += -pg
CXXFLAGS.bc   += -pg
CXXFLAGS.o    += -pg
LDFLAGS.o     += -pg
endif

LDFLAGS.o    += -pthread
LDLIBS       += -ldl -lrt

all: $(EXECS)

clean:
	@find . -name "*.bc" | xargs rm -f
	@find . -name "*.ll" | xargs rm -f
	@find . -name "*.o" | xargs rm -f
	@rm -f $(EXECS)

$(EXECS): %:
	$(LD.llvm) $(LDFLAGS.llvm) -o $@.tx.bc $(filter %.bc,$^)
	$(CXX.ld) $(LDFLAGS.o) -o $@ $@.tx.bc $(STMSUPPORT)/stmsupport.bc $(filter-out %.bc,$^) $(LDLIBS)

%.bc: %.c
	$(CC.bc) $(CFLAGS) $(CFLAGS.bc) -c -o $@ $<

%.bc: %.cpp
	$(CXX.bc) $(CXXFLAGS) $(CXXFLAGS.bc) -c -o $@ $<

%.o: %.c
	$(CC.o) $(CFLAGS) $(CFLAGS.o) -c -o $@ $<

%.o: %.cpp
	$(CXX.o) $(CXXFLAGS) $(CXXFLAGS.o) -c -o $@ $<

BITS   ?= 64
TRIALS ?= 3
CORES  ?= 12
ALGS   ?= OrecELA OrecSandbox

ifdef BIND
CPUSET = 1,3,5,7,9,11,13,15,17,19,21,23
else
CPUSET = 1,3,5,7,9,11,13,15,17,19,21,23,0,2,4,6,8,10,12,14,16,18,20,22
endif

email:
	echo "test complete $$?" | /bin/mail -s "test complete" `whoami`@cs.rochester.edu

