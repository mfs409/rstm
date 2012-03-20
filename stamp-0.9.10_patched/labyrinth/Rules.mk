CFLAGS  += -DUSE_EARLY_RELEASE
LDFLAGS += -lm

PROG       = labyrinth
BENCHNAMES = coordinate grid labyrinth maze router
LIBNAMES   = list mt19937ar pair queue random thread vector
