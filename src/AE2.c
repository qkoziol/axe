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

#include "AE2engine.h"
#include "AE2schedule.h"
#include "AE2task.h"


AE2_error_t
AE2create_engine(size_t num_threads, AE2_engine_t *engine/*out*/)
{
    AE2_engine_int_t *int_engine = NULL;
    AE2_error_t ret_value = AE2_SUCCEED;

    /* Check parameters */
    if(num_threads == 0)
        ERROR;
    if(!engine)
        ERROR;

    if(AE2_engine_create(num_threads, &int_engine) != AE2_SUCCEED)
        ERROR;

    *engine = int_engine;

done:
    return ret_value;
} /* end AE2create_engine() */


/* Note: what happens if the user still has handles open for tasks in this
 * engine? */
AE2_error_t
AE2terminate_engine(AE2_engine_t engine, _Bool wait_all)
{
    AE2_error_t ret_value = AE2_SUCCEED;

    /* Check parameters */
    if(!engine)
        ERROR;

    /* Wait for all tasks to complete, if requested */
    if(wait_all)
        if(AE2_schedule_wait_all(((AE2_engine_int_t *)engine)->schedule) != AE2_SUCCEED)
            ERROR;

    /* Now that all tasks are complete (or cancelled), we can free the engine */
    if(AE2_engine_free(engine) != AE2_SUCCEED)
        ERROR;

done:
    return ret_value;
} /* end AE2terminate_engine() */


AE2_error_t
AE2create_task(AE2_engine_t engine, AE2_task_t *task/*out*/,
    size_t num_necessary_parents, AE2_task_t necessary_parents[],
    size_t num_sufficient_parents, AE2_task_t sufficient_parents[],
    AE2_task_op_t op, void *op_data, AE2_task_free_op_data_t free_op_data)
{
    AE2_task_int_t *int_task = NULL;
    AE2_error_t ret_value = AE2_SUCCEED;

    /* Check parameters */
    if(!engine)
        ERROR;
    if(num_necessary_parents > 0 && !necessary_parents)
        ERROR;
    if(num_sufficient_parents > 0 && !sufficient_parents)
        ERROR;

    /* Create task */
    if(AE2_task_create(engine, &int_task, num_necessary_parents,
            necessary_parents, num_sufficient_parents, sufficient_parents, op,
            op_data, free_op_data) != AE2_SUCCEED)
        ERROR;
    assert(int_task);

    /* If the caller requested a handle, return the pointer to the task,
     * otherwise decrement the reference count because we will throw away our
     * task pointer. */
    if(task)
        *task = int_task;
    else {
#ifdef NAF_DEBUG_REF
        printf("AE2create_task: decr ref: %p", int_task);
#endif /* NAF_DEBUG_REF */
        AE2_task_decr_ref(int_task);
    } /* end else */

done:
    return ret_value;
} /* end AE2create_task() */


/* Note for AE2remove/remove_all: Does the app still need to call AE2finish() or
 * do these functions free the task(s)? */


AE2_error_t
AE2get_op_data(AE2_task_t task, void **op_data/*out*/)
{
    AE2_error_t ret_value = AE2_SUCCEED;

    /* Check parameters */
    if(!task)
        ERROR;
    if(!op_data)
        ERROR;

    /* Get op data */
    AE2_task_get_op_data(task, op_data);

done:
    return ret_value;
} /* end AE2get_op_data() */


AE2_error_t
AE2get_status(AE2_task_t task, AE2_status_t *status/*out*/)
{
    AE2_error_t ret_value = AE2_SUCCEED;

    /* Check parameters */
    if(!task)
        ERROR;
    if(!status)
        ERROR;

    /* Use read/write barriers before and after we retrieve the status, in case
     * the application is using the result to determine a course of action that
     * is only valid for a certain status.  */
    OPA_read_write_barrier();

    /* Get op data */
    AE2_task_get_status(task, status);

    /* Read/write barrier, see above */
    OPA_read_write_barrier();

done:
    return ret_value;
} /* end AE2get_status() */


AE2_error_t
AE2wait(AE2_task_t task)
{
    AE2_error_t ret_value = AE2_SUCCEED;

    /* Check parameters */
    if(!task)
        ERROR;

    /* Wait for task to complete */
    if(AE2_task_wait(task) != AE2_SUCCEED)
        ERROR;

done:
    return ret_value;
} /* end AE2wait() */


AE2_error_t
AE2finish(AE2_task_t task)
{
    AE2_error_t ret_value = AE2_SUCCEED;

    /* Check parameters */
    if(!task)
        ERROR;

    /* Decrement reference count on task, it will be freed if it drops to zero
     */
#ifdef NAF_DEBUG_REF
    printf("AE2finish: decr ref: %p", task);
#endif /* NAF_DEBUG_REF */
    AE2_task_decr_ref(task);

done:
    return ret_value;
} /* end AE2finish() */

