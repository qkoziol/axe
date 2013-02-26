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

#ifndef AE2_H_INCLUDED
#define AE2_H_INCLUDED


/*
 * Public typedefs
 */
typedef struct AE2_task_int_t AE2_task_int_t;
typedef AE2_task_int_t *AE2_task_t;

typedef struct AE2_engine_int_t AE2_engine_int_t;
typedef AE2_engine_int_t *AE2_engine_t;

typedef enum {
    AE2_WAITING_FOR_PARENT,
    AE2_TASK_SCHEDULED,
    AE2_TASK_RUNNING,
    AE2_TASK_DONE,
    AE2_TASK_CANCELED           /* Internal */
} AE2_status_t;

typedef enum {
    AE2_SUCCEED,
    AE2_FAIL
} AE2_error_t;

typedef void (*AE2_task_op_t)(size_t num_necessary_parents,
    AE2_task_t necessary_parents[], size_t num_sufficient_parents,
    AE2_task_t sufficient_parents[], void *op_data);

typedef void (*AE2_task_free_op_data_t)(void *op_data);


/*
 * Public functions
 */
AE2_error_t AE2create_engine(size_t num_threads, AE2_engine_t *engine/*out*/);
AE2_error_t AE2terminate_engine(AE2_engine_t engine, _Bool wait_all);
AE2_error_t AE2create_task(AE2_engine_t engine, AE2_task_t *task/*out*/,
    size_t num_necessary_parents, AE2_task_t necessary_parents[],
    size_t num_sufficient_parents, AE2_task_t sufficient_parents[],
    AE2_task_op_t op, void *op_data, AE2_task_free_op_data_t free_op_data);
AE2_error_t AE2get_op_data(AE2_task_t task, void **op_data/*out*/);
AE2_error_t AE2get_status(AE2_task_t task, AE2_status_t *status/*out*/);
AE2_error_t AE2wait(AE2_task_t task);
AE2_error_t AE2finish(AE2_task_t task);


#endif /* AE2_H_INCLUDED */

