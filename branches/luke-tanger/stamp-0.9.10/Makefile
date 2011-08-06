BENCHMARKS := bayes genome intruder kmeans labyrinth ssca2 vacation yada

.PHONY: all clean $(BENCHMARKS)

all: TARGET := all
all: $(BENCHMARKS)

clean: TARGET := clean
clean: $(BENCHMARKS)

test: TARGET := test
test: $(BENCHMARKS)

$(BENCHMARKS):
	$(MAKE) --directory=$@ $(TARGET)
