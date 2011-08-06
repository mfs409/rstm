BENCHMARKS := bench mesh stamp-0.9.10
CLEANS := $(addsuffix _clean,$(BENCHMARKS))

.PHONY: all clean $(BENCHMARKS) $(CLEANS)

all: TARGET := all
all: $(BENCHMARKS)

clean: TARGET := clean
clean: $(BENCHMARKS)

test: TARGET := test
test: $(BENCHMARKS)

$(BENCHMARKS):
	$(MAKE) --directory=$@ $(TARGET)