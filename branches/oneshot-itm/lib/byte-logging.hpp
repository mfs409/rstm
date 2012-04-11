/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_BYTE_LOGGING_H
#define RSTM_BYTE_LOGGING_H

/**
 *  This file contains everything that you always wanted to know about byte
 *  logging in rstm. Byte logging has a broad impact on a number of important
 *  components.
 */

#if defined(STM_WS_WORDLOG)
// # define ALG_TM_READ_WORD(tx, addr, mask) alg_tm_read_word(tx, addr)
# define ALG_TM_WRITE_WORD(tx, addr, val, mask) alg_tm_write(tx, addr, val)

# define REDO_LOG_ENTRY(addr, val, mask) addr, val
// # define REDO_RAW_CHECK(found, mask) found != 0
// # define REDO_RAW_MERGE(from_log, from_mem, bytes_from_mem) from_mem

# define UNDO_LOG_ENTRY(addr, val, mask) addr, val

#elif defined(STM_WS_BYTELOG)

// # define ALG_TM_READ_WORD(tx, addr, mask) alg_tm_read(tx, addr, mask)
# define ALG_TM_WRITE_WORD(tx, addr, val, mask) alg_tm_write(tx, addr, val, mask)

# define REDO_LOG_ENTRY(addr, val, mask) addr, val, mask
// # define REDO_RAW_CHECK(found, mask) !(found = mask & ~found)
// # define REDO_RAW_MERGE(from_log, from_mem, bytes_from_mem) \
//     (void*)(((uintptr_t)from_log & ~ bytes_from_mem) | \
//             ((uintptr_t)from_mem & bytes_from_mem))

# define UNDO_LOG_ENTRY(addr, val, mask) addr, val, mask
#else
#   error WriteSet logging granularity configuration error.
#endif


#endif // RSTM_BYTE_LOGGING_H
