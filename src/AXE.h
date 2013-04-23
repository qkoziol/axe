/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef AXE_H_INCLUDED
#define AXE_H_INCLUDED

#include <stdint.h>


/*
 * Public typedefs
 */
/* AXE_int_t - handle for an asynchronously executed task */
typedef uint64_t AXE_task_t;

/* AXE_engine_t - handle for an asynchronous engine containing tasks */
typedef struct AXE_engine_int_t AXE_engine_int_t;
typedef AXE_engine_int_t *AXE_engine_t;

/* Attribute struct for modifying engine creation parameters */
typedef struct AXE_engine_attr_t {
    size_t num_threads;         /* The number of threads used for task execution */
    size_t num_buckets;         /* The number of buckets in the id table.  The hash function is simply id % num_buckets */
    size_t num_mutexes;         /* The number of bucket mutexes in the id table */
    AXE_task_t min_id;          /* Minimum id for automatically generated ids */
    AXE_task_t max_id;          /* Maximum id for automatically generated ids */
} AXE_engine_attr_t;

/* The status of a task */
typedef enum {
    AXE_WAITING_FOR_PARENT,     /* The task cannot run yet because not all conditions have been met */
    AXE_TASK_SCHEDULED,         /* The task can be run but has not started running yet */
    AXE_TASK_RUNNING,           /* The task is executing */
    AXE_TASK_DONE,              /* The task is complete */
    AXE_TASK_CANCELED           /* The task was canceled before it could run */
} AXE_status_t;

/* Enum to describe the outcome of AXEremove() and AXEremove_all() */
typedef enum {
    AXE_CANCELED,       /* All tasks were canceled or already complete */
    AXE_NOT_CANCELED,   /* At least one task was not canceled because it was running */
    AXE_ALL_DONE        /* All tasks were already complete */
} AXE_remove_status_t;

/* Return value from API calls */
typedef enum {
    AXE_SUCCEED,        /* Function completed normally */
    AXE_FAIL            /* Function failed and did not complete normally */
} AXE_error_t;

/* This function pointer type refers to the operation routine that is
 * asynchronously executed by the engine for a given task. The memory for the
 * op_data parameter (which includes both the operation’s parameters and its
 * return value) must be managed by the user unless a free_op_data callback is
 * provided to AXEcreate_task().  When an operation is invoked by the execution
 * engine, the engine passes arrays of task handles to the necessary and
 * sufficient parent tasks that have completed and have allow this operation to
 * be invoked.  The array of sufficient parent task handles that is passed in to
 * this callback will only contain those sufficient parent tasks that have
 * completed, and may not include all of the sufficient parent task handles used
 * in the creation call for the task.  The operation must call AXEfinish* on
 * these task handles, after retrieving any information about their tasks (e.g.:
 * op_data) that it requires.
 *
 * The operation routine will only be invoked once for each task, and has no
 * return value. */
typedef void (*AXE_task_op_t)(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *op_data);

/* This function pointer type refers to a routine that is invoked by the engine
 * when the engine no longer needs to use the task and AXEfinish() has been
 * called on all outstanding references to the task.  It is intended to be used
 * to free op_data, and can simply be passed as "free" if that is sufficient. */
typedef void (*AXE_task_free_op_data_t)(void *op_data);


/*
 * Public functions
 */
AXE_error_t AXEengine_attr_init(AXE_engine_attr_t *attr);
AXE_error_t AXEengine_attr_destroy(AXE_engine_attr_t *attr);
AXE_error_t AXEset_num_threads(AXE_engine_attr_t *attr, size_t num_threads);
AXE_error_t AXEget_num_id_threads(const AXE_engine_attr_t *attr,
    size_t *num_threads);
AXE_error_t AXEset_id_range(AXE_engine_attr_t *attr, AXE_task_t min_id,
    AXE_task_t max_id);
AXE_error_t AXEget_id_range(const AXE_engine_attr_t *attr, AXE_task_t *min_id,
    AXE_task_t *max_id);
AXE_error_t AXEset_num_id_buckets(AXE_engine_attr_t *attr, size_t num_buckets);
AXE_error_t AXEget_num_id_buckets(const AXE_engine_attr_t *attr,
    size_t *num_buckets);
AXE_error_t AXEset_num_id_mutexes(AXE_engine_attr_t *attr, size_t num_mutexes);
AXE_error_t AXEget_num_id_mutexes(const AXE_engine_attr_t *attr,
    size_t *num_mutexes);
AXE_error_t AXEcreate_engine(AXE_engine_t *engine/*out*/,
    const AXE_engine_attr_t *attr);
AXE_error_t AXEterminate_engine(AXE_engine_t engine, _Bool wait_all);
AXE_error_t AXEgenerate_task_id(AXE_engine_t engine, AXE_task_t *task);
AXE_error_t AXErelease_task_id(AXE_engine_t engine, AXE_task_t task);
AXE_error_t AXEcreate_task(AXE_engine_t engine, AXE_task_t task,
    size_t num_necessary_parents, AXE_task_t necessary_parents[],
    size_t num_sufficient_parents, AXE_task_t sufficient_parents[],
    AXE_task_op_t op, void *op_data, AXE_task_free_op_data_t free_op_data);
AXE_error_t AXEcreate_barrier_task(AXE_engine_t engine, AXE_task_t task,
    AXE_task_op_t op, void *op_data, AXE_task_free_op_data_t free_op_data);
AXE_error_t AXEremove(AXE_engine_t engine, AXE_task_t task,
    AXE_remove_status_t *remove_status);
AXE_error_t AXEremove_all(AXE_engine_t engine,
    AXE_remove_status_t *remove_status);
AXE_error_t AXEget_op_data(AXE_engine_t engine, AXE_task_t task,
    void **op_data/*out*/);
AXE_error_t AXEget_status(AXE_engine_t engine, AXE_task_t task,
    AXE_status_t *status/*out*/);
AXE_error_t AXEwait(AXE_engine_t engine, AXE_task_t task);
AXE_error_t AXEfinish(AXE_engine_t engine, AXE_task_t task);
AXE_error_t AXEfinish_all(AXE_engine_t engine, size_t num_tasks,
    AXE_task_t task[]);
AXE_error_t AXEbegin_try(void);
AXE_error_t AXEend_try(void);


#endif /* AXE_H_INCLUDED */

