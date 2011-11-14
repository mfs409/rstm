
/* =============================================================================
 *
 * Copyright (C) Stanford University, 2010.  All Rights Reserved.
 * Author: Sungpack Hong and Jared Casper
 *
 * =============================================================================
 */

#ifndef _STAMP_API_FOR_UNPROTECTED_
#define _STAMP_API_FOR_UNPROTECTED_

#define TM_ARG            
#define TM_ARGDECL          

# define TM_STARTUP()
# define TM_SHUTDOWN()
# define TM_THREAD_ENTER()
# define TM_THREAD_EXIT()
# define TM_BEGIN()
# define TM_END()
# define STM_READ(var)      (var)
# define STM_WRITE(var,val) ({var = val; var;})

#endif
