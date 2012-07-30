CFLAGS += -DLIST_NO_DUPLICATES
CFLAGS += -DMAP_USE_AVLTREE
CFLAGS += -DSET_USE_RBTREE

PROG       = yada
BENCHNAMES = coordinate element mesh region yada
LIBNAMES   = avltree heap list mt19937ar pair queue random rbtree thread vector
