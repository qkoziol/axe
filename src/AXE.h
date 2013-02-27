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

#ifndef AXE_H_INCLUDED
#define AXE_H_INCLUDED


/*
 * Public typedefs
 */
typedef struct AXE_task_int_t AXE_task_int_t;
typedef AXE_task_int_t *AXE_task_t;

typedef struct AXE_engine_int_t AXE_engine_int_t;
typedef AXE_engine_int_t *AXE_engine_t;

typedef enum {
    AXE_WAITING_FOR_PARENT,
    AXE_TASK_SCHEDULED,
    AXE_TASK_RUNNING,
    AXE_TASK_DONE,
    AXE_TASK_CANCELED
} AXE_status_t;

typedef enum {
    AXE_SUCCEED,
    AXE_FAIL
} AXE_error_t;

typedef void (*AXE_task_op_t)(size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *op_data);

typedef void (*AXE_task_free_op_data_t)(void *op_data);


/*
 * Public functions
 */
AXE_error_t AXEcreate_engine(size_t num_threads, AXE_engine_t *engine/*out*/);
AXE_error_t AXEterminate_engine(AXE_engine_t engine, _Bool wait_all);
AXE_error_t AXEcreate_task(AXE_engine_t engine, AXE_task_t *task/*out*/,
    size_t num_necessary_parents, AXE_task_t necessary_parents[],
    size_t num_sufficient_parents, AXE_task_t sufficient_parents[],
    AXE_task_op_t op, void *op_data, AXE_task_free_op_data_t free_op_data);
AXE_error_t AXEget_op_data(AXE_task_t task, void **op_data/*out*/);
AXE_error_t AXEget_status(AXE_task_t task, AXE_status_t *status/*out*/);
AXE_error_t AXEwait(AXE_task_t task);
AXE_error_t AXEfinish(AXE_task_t task);


#endif /* AXE_H_INCLUDED */

