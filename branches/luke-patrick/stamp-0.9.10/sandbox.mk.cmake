# -*- Makefile -*-
BENCHMARKS := bayes labyrinth kmeans ssca2 vacation yada # genome intruder
CLEANS     := $(addsuffix _clean,$(BENCHMARKS))

.PHONY: all clean $(BENCHMARKS) $(CLEANS)

all: TARGET := all
all: $(BENCHMARKS)

clean: TARGET := clean
clean: $(BENCHMARKS)

test: TARGET := test
test: $(BENCHMARKS)

$(BENCHMARKS):
	$(MAKE) --directory=$@ -f sandbox.mk $(TARGET)
