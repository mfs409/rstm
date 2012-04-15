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
#if   defined(STM_WS_WORDLOG)
#elif defined(STM_WS_BYTELOG)
#else
#   error WriteSet logging granularity configuration error.
#endif


#endif // RSTM_BYTE_LOGGING_H
