
/* =============================================================================
 *
 * Copyright (C) Stanford University, 2010.  All Rights Reserved.
 * Author: Sungpack Hong and Jared Casper
 *
 * =============================================================================
 */

#ifndef _STAMP_API_FOR_RSTM_
#define _STAMP_API_FOR_RSTM_

#include "tm.h"

# define STM_READ(var)      TM_SHARED_READ_L(var)
# define STM_WRITE(var,val) TM_SHARED_WRITE_L(var, val)

#endif
