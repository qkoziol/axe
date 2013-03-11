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

#ifndef AXE_THREADPOOL_H_INCLUDED
#define AXE_THREADPOOL_H_INCLUDED

#include "AXEprivate.h"


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
    AXE_thread_t **thread/*out*/);
void AXE_thread_pool_release(AXE_thread_t *thread);
AXE_error_t AXE_thread_pool_launch(AXE_thread_t *thread,
    AXE_thread_op_t thread_op, void *thread_op_data);
AXE_error_t AXE_thread_pool_free(AXE_thread_pool_t *thread_pool);


#endif /* AXE_THREADPOOL_H_INCLUDED */

