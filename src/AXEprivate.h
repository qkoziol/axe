/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have          *
 * access to either file, you may request a copy from help@hdfgroup.org.     *
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
 * Private macros
 */
#define ERROR \
do { \
    fprintf(stderr, "FAILED in " __FILE__ " at line %d\n", __LINE__); \
    ret_value = AXE_FAIL; \
    goto done; \
} while(0)

#define ERROR_RET(RET) \
do { \
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


#endif /* AXE_PRIVATE_H_INCLUDED */

