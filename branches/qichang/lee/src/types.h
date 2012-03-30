/* =============================================================================
 *
 * types.h
 * -- definitions of some types
 *
 * =============================================================================
 *
 * Copyright (C) Stanford University, 2006.  All Rights Reserved.
 * Author: Chi Cao Minh
 *
 * =============================================================================
 */


#ifndef TYPES_H
#define TYPES_H 1


#ifdef __cplusplus
extern "C" {
#endif


#ifdef SIMULATOR
#  undef TRUE
#  undef FALSE
#  undef bool
#endif


typedef unsigned long ulong_t;

enum {
    FALSE = 0,
    TRUE  = 1
};

typedef long bool_t;


#ifdef __cplusplus
}
#endif


#endif /* TYPES_H */


/* =============================================================================
 *
 * End of types.h
 *
 * =============================================================================
 */
