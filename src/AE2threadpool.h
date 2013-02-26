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

#ifndef AE2_THREADPOOL_H_INCLUDED
#define AE2_THREADPOOL_H_INCLUDED

#include "AE2private.h"


/*
 * Typedefs
 */
typedef struct AE2_thread_pool_t AE2_thread_pool_t;

typedef struct AE2_thread_t AE2_thread_t;

typedef AE2_error_t (*AE2_thread_op_t)(void *op_data);


/*
 * Functions
 */
AE2_error_t AE2_thread_pool_create(size_t num_threads,
    AE2_thread_pool_t **thread_pool/*out*/);
AE2_error_t AE2_thread_pool_try_acquire(AE2_thread_pool_t *thread_pool,
    AE2_thread_t **thread/*out*/);
void AE2_thread_pool_release(AE2_thread_t *thread);
AE2_error_t AE2_thread_pool_launch(AE2_thread_t *thread,
    AE2_thread_op_t thread_op, void *thread_op_data);
AE2_error_t AE2_thread_pool_free(AE2_thread_pool_t *thread_pool);


#endif /* AE2_THREADPOOL_H_INCLUDED */

