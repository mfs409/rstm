CFLAGS += -DLIST_NO_DUPLICATES
CFLAGS += -DCHUNK_STEP1=12

PROG       = genome
BENCHNAMES = gene genome segments sequencer table
LIBNAMES   = bitmap hash hashtable pair random list mt19937ar thread vector
