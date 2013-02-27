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

#include "AXEengine.h"
#include "AXEschedule.h"
#include "AXEtask.h"


AXE_error_t
AXEcreate_engine(size_t num_threads, AXE_engine_t *engine/*out*/)
{
    AXE_engine_int_t *int_engine = NULL;
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(num_threads == 0)
        ERROR;
    if(!engine)
        ERROR;

    if(AXE_engine_create(num_threads, &int_engine) != AXE_SUCCEED)
        ERROR;

    *engine = int_engine;

done:
    return ret_value;
} /* end AXEcreate_engine() */


/* Note: what happens if the user still has handles open for tasks in this
 * engine? */
AXE_error_t
AXEterminate_engine(AXE_engine_t engine, _Bool wait_all)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!engine)
        ERROR;

    /* Wait for all tasks to complete, if requested */
    if(wait_all)
        if(AXE_schedule_wait_all(((AXE_engine_int_t *)engine)->schedule) != AXE_SUCCEED)
            ERROR;

    /* Now that all tasks are complete (or cancelled), we can free the engine */
    if(AXE_engine_free(engine) != AXE_SUCCEED)
        ERROR;

done:
    return ret_value;
} /* end AXEterminate_engine() */


AXE_error_t
AXEcreate_task(AXE_engine_t engine, AXE_task_t *task/*out*/,
    size_t num_necessary_parents, AXE_task_t necessary_parents[],
    size_t num_sufficient_parents, AXE_task_t sufficient_parents[],
    AXE_task_op_t op, void *op_data, AXE_task_free_op_data_t free_op_data)
{
    AXE_task_int_t *int_task = NULL;
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!engine)
        ERROR;
    if(num_necessary_parents > 0 && !necessary_parents)
        ERROR;
    if(num_sufficient_parents > 0 && !sufficient_parents)
        ERROR;

    /* Create task */
    if(AXE_task_create(engine, &int_task, num_necessary_parents,
            necessary_parents, num_sufficient_parents, sufficient_parents, op,
            op_data, free_op_data) != AXE_SUCCEED)
        ERROR;
    assert(int_task);

    /* If the caller requested a handle, return the pointer to the task,
     * otherwise decrement the reference count because we will throw away our
     * task pointer. */
    if(task)
        *task = int_task;
    else {
#ifdef AXE_DEBUG_REF
        printf("AXEcreate_task: decr ref: %p", int_task);
#endif /* AXE_DEBUG_REF */
        AXE_task_decr_ref(int_task);
    } /* end else */

done:
    return ret_value;
} /* end AXEcreate_task() */


/* Note for AXEremove/remove_all: Does the app still need to call AXEfinish() or
 * do these functions free the task(s)? */


AXE_error_t
AXEget_op_data(AXE_task_t task, void **op_data/*out*/)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!task)
        ERROR;
    if(!op_data)
        ERROR;

    /* Get op data */
    AXE_task_get_op_data(task, op_data);

done:
    return ret_value;
} /* end AXEget_op_data() */


AXE_error_t
AXEget_status(AXE_task_t task, AXE_status_t *status/*out*/)
{
    AXE_error_t ret_value = AXE_SUCCEED;

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
    AXE_task_get_status(task, status);

    /* Read/write barrier, see above */
    OPA_read_write_barrier();

done:
    return ret_value;
} /* end AXEget_status() */


AXE_error_t
AXEwait(AXE_task_t task)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!task)
        ERROR;

    /* Wait for task to complete */
    if(AXE_task_wait(task) != AXE_SUCCEED)
        ERROR;

done:
    return ret_value;
} /* end AXEwait() */


AXE_error_t
AXEfinish(AXE_task_t task)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!task)
        ERROR;

    /* Decrement reference count on task, it will be freed if it drops to zero
     */
#ifdef AXE_DEBUG_REF
    printf("AXEfinish: decr ref: %p", task);
#endif /* AXE_DEBUG_REF */
    AXE_task_decr_ref(task);

done:
    return ret_value;
} /* end AXEfinish() */


AXE_error_t
AXEfinish_all(size_t num_tasks, AXE_task_t task[])
{
    size_t i;
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!task)
        ERROR;

    /* Iterate over all tasks */
    for(i = 0; i < num_tasks; i++) {
        /* Check that task exists */
        if(!task[i])
            ERROR;

        /* Decrement reference count on task, it will be freed if it drops to zero
         */
#ifdef AXE_DEBUG_REF
    printf("AXEfinish_all: decr ref: %p", task);
#endif /* AXE_DEBUG_REF */
        AXE_task_decr_ref(task[i]);
    } /* end for */

done:
    return ret_value;
} /* end AXEfinish_all() */

