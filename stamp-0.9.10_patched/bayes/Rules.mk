CFLAGS += -DLIST_NO_DUPLICATES
CFLAGS += -DLEARNER_TRY_REMOVE
CFLAGS += -DLEARNER_TRY_REVERSE

PROG       = bayes
BENCHNAMES = adtree bayes data learner net sort
LIBNAMES   = bitmap list mt19937ar queue random thread vector
