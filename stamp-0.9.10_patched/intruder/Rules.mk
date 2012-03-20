CFLAGS += -DMAP_USE_RBTREE

PROG       = intruder
BENCHNAMES = decoder detector dictionary intruder packet preprocessor stream
LIBNAMES   = list mt19937ar pair queue random rbtree thread vector
