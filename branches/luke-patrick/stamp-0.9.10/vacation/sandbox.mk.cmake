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
