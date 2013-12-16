/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <AXEtest.h>


/*
 * Macros
 */
/* Uncomment this line to make the tests run much faster, at the expense of
 * increasing the likelyhood of missing an intermittent failure */
//#define TEST_EXPRESS

/* Macros for printing standard messages and issuing errors */
#define AT()            printf ("        at %s:%d in %s()...\n", __FILE__, __LINE__, __FUNCTION__)
#define TESTING(WHAT) \
do { \
        printf("Testing %-62s", WHAT); \
    fflush(stdout); \
} while(0)
#define PASSED()        do {puts(" PASSED");fflush(stdout);} while(0)
#define FAILED()        do {puts("*FAILED*");fflush(stdout);} while(0)
#define WARNING()       do {puts("*WARNING*");fflush(stdout);} while(0)
#define SKIPPED()       do {puts(" -SKIP-");fflush(stdout);} while(0)
#define TEST_ERROR      do {FAILED(); AT(); goto error;} while(0)

/* Maximum number of threads that any test could possibly want to run
 * concurrently */
#define MAX_THREADS_USABLE 221

/* Name of the file that contains the maximum number of threads */
#define MAX_NTHREADS_FILENAME "max_nthreads.txt"

/* Global declarations for variables used to limit the number of threads */
#define MAX_NTHREADS_DECL \
    int max_nthreads_g; \
    int current_nthreads_g; \
    int max_nthreads_nwaiters_g; \
    pthread_cond_t max_nthreads_cond_g; \
    pthread_mutex_t max_nthreads_mutex_g

/* Macro to initialize variables declared in MAX_NTHREADS_DEFINE */
#define MAX_NTHREADS_INIT(ERROR_STATEMENT) do { \
    FILE *_infile; \
    if(NULL == (_infile = fopen(MAX_NTHREADS_FILENAME, "r"))) { \
        ERROR_STATEMENT; \
    } /* end if */ \
    if(1 != fscanf(_infile, "%d", &max_nthreads_g)) { \
        ERROR_STATEMENT; \
    } /* end if */ \
    if(0 != fclose(_infile)) { \
        ERROR_STATEMENT; \
    } /* end if */ \
    current_nthreads_g = 1; \
    max_nthreads_nwaiters_g = 0; \
    if(0 != pthread_cond_init(&max_nthreads_cond_g, NULL)) { \
        ERROR_STATEMENT; \
    } /* end if */ \
    if(0 != pthread_mutex_init(&max_nthreads_mutex_g, NULL)) { \
        ERROR_STATEMENT; \
    } /* end if */ \
} while(0)

/* Macro to free variables initialized in MAX_NTHREADS_INIT */
#define MAX_NTHREADS_FREE(ERROR_STATEMENT) do { \
    if(0 != pthread_cond_destroy(&max_nthreads_cond_g)) { \
        ERROR_STATEMENT; \
    } /* end if */ \
    if(0 != pthread_mutex_destroy(&max_nthreads_mutex_g)) { \
        ERROR_STATEMENT; \
    } /* end if */ \
    if(current_nthreads_g != 1) { \
        ERROR_STATEMENT; \
    } /* end if */ \
} while(0)

/* Macro to reserve NUM_REQUESTED threads.  Blocks until threads are available.
 */
#define MAX_NTHREADS_RESERVE(NUM_REQUESTED, ERROR_STATEMENT) do { \
    if(max_nthreads_g < MAX_THREADS_USABLE) { \
        assert((NUM_REQUESTED) + 1 <= max_nthreads_g); \
        if(0 != pthread_mutex_lock(&max_nthreads_mutex_g)) { \
            ERROR_STATEMENT; \
        } /* end if */ \
        max_nthreads_nwaiters_g++; \
        while(current_nthreads_g + (NUM_REQUESTED) > max_nthreads_g) \
            if(0 != pthread_cond_wait(&max_nthreads_cond_g, &max_nthreads_mutex_g)) { \
                ERROR_STATEMENT; \
            } /* end if */ \
        max_nthreads_nwaiters_g--; \
        current_nthreads_g += (NUM_REQUESTED); \
        if(0 != pthread_mutex_unlock(&max_nthreads_mutex_g)) { \
            ERROR_STATEMENT; \
        } /* end if */ \
    } /* end if */ \
} while(0)

/* Macro to release NUM_RELEASED threads */
#define MAX_NTHREADS_RELEASE(NUM_RELEASED, ERROR_STATEMENT) do { \
    if(max_nthreads_g < MAX_THREADS_USABLE) { \
        assert((NUM_RELEASED) < current_nthreads_g); \
        if(0 != pthread_mutex_lock(&max_nthreads_mutex_g)) { \
            ERROR_STATEMENT; \
        } /* end if */ \
        current_nthreads_g -= (NUM_RELEASED); \
        if(max_nthreads_nwaiters_g > 0) \
            if(0 != pthread_cond_broadcast(&max_nthreads_cond_g)) { \
                ERROR_STATEMENT; \
            } /* end if */ \
        if(0 != pthread_mutex_unlock(&max_nthreads_mutex_g)) { \
            ERROR_STATEMENT; \
        } /* end if */ \
    } /* end if */ \
} while(0)

/* Macro to try to check if we can run NUM_REQUESTED threads.  Treated as an if
 * statement. */
#define MAX_NTHREADS_CHECK_STATIC_IF(NUM_REQUESTED) \
    if(current_nthreads_g + (NUM_REQUESTED) <= max_nthreads_g)

