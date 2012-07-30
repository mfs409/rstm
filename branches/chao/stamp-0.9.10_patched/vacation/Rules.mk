CFLAGS += -DLIST_NO_DUPLICATES
CFLAGS += -DMAP_USE_RBTREE

PROG       = vacation
BENCHNAMES = client customer manager reservation vacation
LIBNAMES   = list pair mt19937ar random rbtree thread
