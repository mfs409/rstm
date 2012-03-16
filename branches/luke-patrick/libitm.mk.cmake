# -*- Makefile -*-
CC    := gcc
CXX   := g++
VPATH := @CMAKE_CURRENT_SOURCE_DIR@/libstm:@CMAKE_CURRENT_SOURCE_DIR@/libstm/algs:@CMAKE_CURRENT_SOURCE_DIR@/libstm/policies:@CMAKE_CURRENT_SOURCE_DIR@/libitm2stm:@CMAKE_CURRENT_SOURCE_DIR@/libitm2stm/arch/x86_64

CXXFLAGS = -I@CMAKE_SOURCE_DIR@ -I@CMAKE_SOURCE_DIR@/include -I@CMAKE_BINARY_DIR@/include -Wall -m64 -fno-rtti -fno-exceptions -g

ifdef DEBUG
OPT_O = -O0
else
OPT_O = -O3
endif

OBJECTS := libstm/txthread.o \
	libstm/inst.o \
	libstm/types.o \
	libstm/profiling.o \
	libstm/WBMMPolicy.o \
	libstm/irrevocability.o \
	libstm/shadow-signals.o \
	libstm/signals.o \
	libstm/validation.o \
	libstm/algs.o \
	libstm/biteager.o \
	libstm/biteagerredo.o \
	libstm/bitlazy.o \
	libstm/byear.o \
	libstm/byeau.o \
	libstm/byteeager.o \
	libstm/byteeagerredo.o \
	libstm/bytelazy.o \
	libstm/cgl.o \
	libstm/ctoken.o \
	libstm/ctokenturbo.o \
	libstm/llt.o \
	libstm/mcs.o \
	libstm/nano.o \
	libstm/norec.o \
	libstm/norecprio.o \
	libstm/oreau.o \
	libstm/orecala.o \
	libstm/oreceager.o \
	libstm/oreceagerredo.o \
	libstm/orecela.o \
	libstm/orecfair.o \
	libstm/oreclazy.o \
	libstm/orecsandbox.o \
	libstm/pipeline.o \
	libstm/profiletm.o \
	libstm/ringala.o \
	libstm/ringsw.o \
	libstm/serial.o \
	libstm/profileapp.o \
	libstm/swiss.o \
	libstm/ticket.o \
	libstm/tli.o \
	libstm/tml.o \
	libstm/tmllazy.o \
	libstm/cbr.o \
	libstm/policies.o \
	libstm/static.o \
	libitm2stm/BlockOperations.o \
	libitm2stm/Scope.o \
	libitm2stm/Transaction.o \
	libitm2stm/libitm-5.1,5.o \
	libitm2stm/libitm-5.2.o \
	libitm2stm/libitm-5.3.o \
	libitm2stm/libitm-5.4.o \
	libitm2stm/libitm-5.7.o \
	libitm2stm/libitm-5.8.o \
	libitm2stm/libitm-5.9.o \
	libitm2stm/libitm-5.10.o \
	libitm2stm/libitm-5.11.o \
	libitm2stm/libitm-5.12.o \
	libitm2stm/libitm-5.13,14.o \
	libitm2stm/libitm-5.15.o \
	libitm2stm/libitm-5.16.o \
	libitm2stm/libitm-5.17.o \
	libitm2stm/_ITM_beginTransaction_no_td.o \
	libitm2stm/checkpoint_restore.o \
	libitm2stm/dtmc.o \
	libitm2stm/Checkpoint.o

all: libitm2stm/libitm.a

clean:
	@rm -f $(OBJECTS)
	@rm -f libitm2stm/libitm.a

libitm2stm/libitm.a: $(OBJECTS)
	$(AR) rcs $@ $^

libstm/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -msse2 $(OPT_O) -o $@ -c $<

libitm2stm/%.o: %.cpp
	$(CXX) -D_ITM_DTMC -I@CMAKE_SOURCE_DIR@/libitm2stm/arch/x86_64 -Wno-invalid-offsetof -Wno-strict-aliasing $(CXXFLAGS) -msse4.2 $(OPT_O) -o $@ -c $<

libitm2stm/%.o: %.S
	$(CC) -I@CMAKE_SOURCE_DIR@/libitm2stm/arch $(OPT_O) -o $@ -c $<

# hack
libstm/signals.o: CXXFLAGS := -I@CMAKE_SOURCE_DIR@/libitm2stm/arch/x86_64 $(CXXFLAGS)