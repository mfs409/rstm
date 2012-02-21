CC     := llvm-gcc
CXX    := llvm-g++
TMLINK := tmlink
STAMP  := /u/luked/rstm-branches/patrick/stamp-0.9.10
VPATH  := ${STAMP}/lib:${STAMP}/vacation

STMLIB := ../../libitm2stm
STMSUPPORT := $(dir $(shell which ${TMLINK}))../lib

CFLAGS   = -I${STAMP}/lib
CFLAGS  += -DLIST_NO_DUPLICATES -DMAP_USE_RBTREE -DDTMC
CFLAGS  += -fgnu-tm -emit-llvm
CFLAGS  += -O3

CXXFLAGS := ${CFLAGS} #-fno-exceptions

LDFLAGS  = -stmlib=${STMLIB}
LDFLAGS += -stmsupport=${STMSUPPORT}
LDFLAGS += -tm-support-file=${STMLIB}/libtanger-stm.support
LDFLAGS += -internalize-public-api-file=${STMLIB}/libtanger-stm.public-symbols

LDLIBS  := -lrt

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
