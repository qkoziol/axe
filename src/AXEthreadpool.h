/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef AXE_THREADPOOL_H_INCLUDED
#define AXE_THREADPOOL_H_INCLUDED

#include "AXEprivate.h"


/*
 * Macros
 */
/* Default number of threads in a thread pool*/
#define AXE_THREAD_POOL_NUM_THREADS_DEF 8


/*
 * Typedefs
 */
typedef struct AXE_thread_pool_t AXE_thread_pool_t;

typedef struct AXE_thread_t AXE_thread_t;

/* Function signature for internal worker tasks launched by the thread pool */
typedef AXE_error_t (*AXE_thread_op_t)(void *op_data);


/*
 * Functions
 */
AXE_error_t AXE_thread_pool_create(size_t num_threads,
    AXE_thread_pool_t **thread_pool/*out*/);
AXE_error_t AXE_thread_pool_try_acquire(AXE_thread_pool_t *thread_pool,
    _Bool exclusive_waiter, AXE_thread_t **thread/*out*/);
AXE_error_t AXE_thread_pool_release(AXE_thread_t *thread);
AXE_error_t AXE_thread_pool_running(AXE_thread_pool_t *thread_pool);
void AXE_thread_pool_sleeping(AXE_thread_pool_t *thread_pool);
AXE_error_t AXE_thread_pool_launch(AXE_thread_t *thread,
    AXE_thread_op_t thread_op, void *thread_op_data);
AXE_error_t AXE_thread_pool_free(AXE_thread_pool_t *thread_pool);


#endif /* AXE_THREADPOOL_H_INCLUDED */

