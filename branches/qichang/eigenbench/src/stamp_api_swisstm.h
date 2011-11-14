/* =============================================================================
 *
 * Copyright (C) Stanford University, 2010.  All Rights Reserved.
 * Author: Sungpack Hong and Jared Casper
 *
 * =============================================================================
 */

#ifndef _STAMP_API_FOR_SWISSTM_
#define _STAMP_API_FOR_SWISSTM_

// include from swisstm release package
# include <stm.h>
# include <atomic_ops.h>

#define BEGIN_TRANSACTION             \
	if(sigsetjmp(*wlpdstm_get_long_jmp_buf(), 0) != LONG_JMP_ABORT_FLAG) {   \
        wlpdstm_start_tx()

#define BEGIN_TRANSACTION_ID(TX_ID)   \
    if(sigsetjmp(*wlpdstm_get_long_jmp_buf(), 0) != LONG_JMP_ABORT_FLAG) {   \
		wlpdstm_start_tx_id(TX_ID)

#define END_TRANSACTION					\
		wlpdstm_commit_tx();			\
	}

#define BEGIN_TRANSACTION_DESC			\
	if(sigsetjmp(*wlpdstm_get_long_jmp_buf_desc(tx), 0) != LONG_JMP_ABORT_FLAG) {	\
		wlpdstm_start_tx_desc(tx)

#define BEGIN_TRANSACTION_DESC_ID(TX_ID)	\
	if(sigsetjmp(*wlpdstm_get_long_jmp_buf_desc(tx), 0) != LONG_JMP_ABORT_FLAG) {	\
		wlpdstm_start_tx_id_desc(tx, TX_ID)

#define END_TRANSACTION_DESC				\
		wlpdstm_commit_tx_desc(tx);			\
	}

        
#define STM_THREAD_T    tx_desc
#define STM_SELF        tx

#define STM_STARTUP()   wlpdstm_global_init()
#define STM_SHUTDOWN()   wlpdstm_print_stats()
#define STM_THREAD_ENTER()  wlpdstm_thread_init(); STM_THREAD_T* STM_SELF = wlpdstm_get_tx_desc()
#define STM_FREE_THREAD(IGNORE) /*nothing*/
#define STM_MALLOC(size)     wlpdstm_tx_malloc(size)
#define STM_FREE(ptr)        wlpdstm_tx_free(ptr, sizeof(ptr))

#define STM_BEGIN()       BEGIN_TRANSACTION_DESC
#define STM_END()         END_TRANSACTION
#define STM_BEGIN_WR    STM_BEGIN
#define STM_BEGIN_RO    STM_BEGIN
#define STM_RESTART()   wlpdstm_restart_tx()

#define STM_READ(var)   wlpdstm_read_word_desc(STM_SELF, (Word *) (&(var)))
#define STM_WRITE(var,val)   wlpdstm_write_word_desc(STM_SELF, (Word *) (&(var)), (Word)val)

#define STM_READ_P  STM_READ
#define STM_READ_F  STM_READ

#define STM_WRITE_P  STM_WRITE
#define STM_WRITE_F  STM_WRITE

#define STM_LOCAL_WRITE(var, val)   ({var = val; var;})
#define STM_LOCAL_WRITE_P       STM_LOCAL_WRITE
#define STM_LOCAL_WRITE_F       STM_LOCAL_WRITE

#define TM_ARG              STM_SELF,
#define TM_ARGDECL          STM_THREAD_T* TM_ARG


# define TM_STARTUP()       STM_STARTUP()
# define TM_SHUTDOWN()      STM_SHUTDOWN()
# define TM_THREAD_ENTER()  STM_THREAD_ENTER();
# define TM_THREAD_EXIT()   STM_FREE_THREAD(STM_SELF)
# define TM_BEGIN()         STM_BEGIN_WR()
# define TM_END()           STM_END()

#endif
