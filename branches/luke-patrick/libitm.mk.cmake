# -*- Makefile -*-
CC    := gcc
CXX   := g++
VPATH := @CMAKE_CURRENT_SOURCE_DIR@/libstm:@CMAKE_CURRENT_SOURCE_DIR@/libstm/algs:@CMAKE_CURRENT_SOURCE_DIR@/libstm/policies:@CMAKE_CURRENT_SOURCE_DIR@/libitm2stm:@CMAKE_CURRENT_SOURCE_DIR@/libitm2stm/arch/x86_64

CXXFLAGS  = -Wall -m64

ifdef DEBUG
CXXFLAGS += -O0 -ggdb
else
CXXFLAGS += -O3
endif

LIBSTM_OBJECTS := txthread.o \
	inst.o \
	types.o \
	profiling.o \
	WBMMPolicy.o \
	irrevocability.o \
	shadow-signals.o \
	signals.o \
	validation.o \
	algs.o \
	biteager.o \
	biteagerredo.o \
	bitlazy.o \
	byear.o \
	byeau.o \
	byteeager.o \
	byteeagerredo.o \
	bytelazy.o \
	cgl.o \
	ctoken.o \
	ctokenturbo.o \
	llt.o \
	mcs.o \
	nano.o \
	norec.o \
	norecprio.o \
	oreau.o \
	orecala.o \
	oreceager.o \
	oreceagerredo.o \
	orecela.o \
	orecfair.o \
	oreclazy.o \
	orecsandbox.o \
	pipeline.o \
	profiletm.o \
	ringala.o \
	ringsw.o \
	serial.o \
	profileapp.o \
	swiss.o \
	ticket.o \
	tli.o \
	tml.o \
	tmllazy.o \
	cbr.o \
	policies.o \
	static.o

LIBITM2STM_OBJECTS = BlockOperations.o \
	Scope.o \
	Transaction.o \
	libitm-5.1,5.o \
	libitm-5.2.o \
	libitm-5.3.o \
	libitm-5.4.o \
	libitm-5.7.o \
	libitm-5.8.o \
	libitm-5.9.o \
	libitm-5.10.o \
	libitm-5.11.o \
	libitm-5.12.o \
	libitm-5.13,14.o \
	libitm-5.15.o \
	libitm-5.16.o \
	libitm-5.17.o \
	_ITM_beginTransaction.o \
	_ITM_beginTransaction_no_td.o \
	checkpoint_restore.o

all: libitm.a

clean:
	@rm -f $(LIBSTM_OBJECTS)
	@rm -f $(LIBITM2STM_OBJECTS)
	@rm -f libitm.a

libitm.a: $(LIBSTM_OBJECTS) $(LIBITM2STM_OBJECTS)
	$(AR) rcs $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

%o: %.S
	$(CC) $(ASFLAGS) -o $@ -c $<

$(LIBSTM_OBJECTS): CXXFLAGS := -I@CMAKE_SOURCE_DIR@ -I@CMAKE_SOURCE_DIR@/include -I@CMAKE_BINARY_DIR@/include -msse2 $(CXXFLAGS)

$(LIBITM2STM_OBJECTS): CXXFLAGS := -I@CMAKE_SOURCE_DIR@ -I@CMAKE_SOURCE_DIR@/include -I@CMAKE_BINARY_DIR@/include -I@CMAKE_SOURCE_DIR@/libitm2stm/arch/x86_64 $(CXXFLAGS) -Wno-invalid-offsetof -Wno-strict-aliasing

$(LIBITM2STM_OBJECTS): ASFLAGS := -I@CMAKE_SOURCE_DIR@/libitm2stm/arch