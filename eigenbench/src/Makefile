
# Directory to STM
TL2_DIR = ../../tl2
SWISSTM_DIR = ../../swisstm
CFLAGS = -g -O2 

#CC = gcc

.PHONY: default
default: eigenbench-unp eigenbench-tl2 eigenbench-swisstm

eigenbench-tl2: eigenbench.c thread.c main.c eigenbench.h
	$(CC) $(CFLAGS) -DTL2 -I$(TL2_DIR) -o $@ $^ -L$(TL2_DIR) -ltl2 -lpthread 

eigenbench-swisstm: eigenbench.c thread.c main.c eigenbench.h
	$(CC) $(CFLAGS) -DSWISSTM -I$(SWISSTM_DIR)/stm/include -I$(SWISSTM_DIR)/atomic_ops/include -o $@ $^ -L$(SWISSTM_DIR)/stm/lib -lwlpdstm -lrt -lpthread

eigenbench-unp: eigenbench.c thread.c main.c eigenbench.h
	$(CC) $(CFLAGS) -DUNPROTECTED -o $@ $^ -lpthread 

clean:
	\rm eigenbench-tl2 eigenbench-unp eigenbench-swisstm

