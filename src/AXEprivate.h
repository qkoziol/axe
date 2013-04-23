/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef AXE_PRIVATE_H_INCLUDED
#define AXE_PRIVATE_H_INCLUDED

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
/* Need to include sched.h if using sched_yield(), otherwise need __USE_GNU for
 * pthread_yield().  Prefer sched_yield() to pthread_yield() due to it being
 * standard. */
#if defined(HAVE_SCHED_YIELD)
#  include <sched.h>
#elif defined(HAVE_PTHREAD_YIELD)
#  define __USE_GNU
#endif
#include <pthread.h>
#include <opa_primitives.h>
#include <opa_queue.h>

#include "AXE.h"


/*
 * Private global variables
 */
extern OPA_int_t AXE_quiet_g;
extern const AXE_engine_attr_t AXE_engine_attr_def_g;


/*
 * Private macros
 */
/* Macro to report an error and return AXE_FAIL in most functions */
#define ERROR \
do { \
    if(OPA_load_int(&AXE_quiet_g) == 0) \
        fprintf(stderr, "FAILED in " __FILE__ " at line %d\n", __LINE__); \
    ret_value = AXE_FAIL; \
    goto done; \
} while(0)

/* Version of ERROR where a different value must be returned */
#define ERROR_RET(RET) \
do { \
    if(OPA_load_int(&AXE_quiet_g) == 0) \
        fprintf(stderr, "FAILED in " __FILE__ " at line %d\n", __LINE__); \
    ret_value = RET; \
    goto done; \
} while(0)

#define TRUE 1
#define FALSE 0

/* Define the macro to use for yielding the current thread (to others) */
#if defined(HAVE_SCHED_YIELD)
#  include <sched.h>
#  define AXE_YIELD() (void)sched_yield()
#elif defined(HAVE_PTHREAD_YIELD)
#  define AXE_YIELD() pthread_yield()
#else
#  define OPA_TEST_YIELD() (void)0
#endif

/* Debugging */
//#define AXE_DEBUG
//#define AXE_DEBUG_REF
//#define AXE_DEBUG_LOCK
//#define AXE_DEBUG_NTASKS
//#define AXE_DEBUG_PERF

#ifdef AXE_DEBUG_PERF
extern OPA_int_t AXE_debug_nspins_add;
extern OPA_int_t AXE_debug_nspins_finish;
extern OPA_int_t AXE_debug_nadds;
#endif /* AXE_DEBUG_PERF */


#endif /* AXE_PRIVATE_H_INCLUDED */

