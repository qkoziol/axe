/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "AXEengine.h"
#include "AXEschedule.h"
#include "AXEtask.h"


/*
 * Global variables
 */
OPA_int_t AXE_quiet_g = OPA_INT_T_INITIALIZER(0); /* Ref count for number of calls to AXE_begin_try() */


/*-------------------------------------------------------------------------
 * Function:    AXEcreate_engine
 *
 * Purpose:     Create a new engine for asynchronous execution, with a
 *              thread pool containing the specified number of threads.
 *              An engine must be created before any tasks can be created.
 *              The engine will always create and maintain num_threads
 *              threads, with no extra threads set aside to, for example,
 *              schedule tasks.  All task scheduling is done on-demand by
 *              application threads (in AXEcreate_task() and
 *              AXEcreate_barrier_task()) and by the task exection
 *              threads.
 *
 *              It is guaranteed that there will always be exactly
 *              num_threads available for execution (with the possibiliy
 *              of a short wait if the thread is performing its scheduling
 *              duties), until AXEterminate_engine() is called.
 *              Therefore, if the application algorithm requires
 *              N tasks to run concurrently in order to proceed, it is
 *              safe to set num_threads to N.
 *
 *              The handle for the newly created engine is returned in
 *              *engine.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
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


/*-------------------------------------------------------------------------
 * Function:    AXEterminate_engine
 *
 * Purpose:     Terminate an asynchronous execution engine.  The engine
 *              will first deal with uncompleted tasks.  If the function
 *              parameter wait_all is set to true, then this function
 *              blocks the program execution until all uncompleted tasks
 *              complete.  Otherwise, tasks that have not begun running
 *              will be canceled and the function blocks until all running
 *              tasks complete. Afterwards, all allocated resources will
 *              be released and the instance engine terminates.
 *
 *              Any concurrent or subsequent use of this engine or the
 *              tasks within it is an error and will result in undefined
 *              behavior.  An exception to this rule is made if wait_all
 *              is true, in which case the engine and tasks may be
 *              manipulated as normal while tasks are still running.  It
 *              is the application's responsibility to make sure that
 *              tasks are still running.  It is safe to call AXEwait() in
 *              this case, even if the task waited on may be the last task
 *              to complete (again as long as tasks are still running when
 *              calling AXEwait()).
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
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


/*-------------------------------------------------------------------------
 * Function:    AXEcreate_task
 *
 * Purpose:     Create a new task in the engine identified by engine,
 *              setting its operation routine to op with parameter
 *              op_data.  This task may depend on other tasks, specified
 *              in the necessary_parents array of size
 *              num_necessary_parents and the sufficient_parents array of
 *              size num_sufficient_parents.  All tasks in the
 *              necessary_parents array must complete and at least one of
 *              the tasks in the sufficient_parents array must complete
 *              before the engine will execute this task, but either of
 *              the necessary or sufficient sets of parent tasks (or both)
 *              can be empty.  If any parent tasks complete before the new
 *              task is inserted, they will still be included in the
 *              arrays passed to the op routine when it is invoked.
 *
 *              If the op parameter is NULL, no operation will be invoked
 *              and this event is solely a placeholder in the graph for
 *              other events to depend on.  Tasks with no parent
 *              dependencies (i.e.: root nodes in the DAG) can be inserted
 *              by setting both of the num_parents parameters to 0.  If
 *              not set to NULL, the free_op_data callback will be invoked
 *              with the task’s op_data pointer when all the outstanding
 *              references to the new task are released (with AXEfinish*)
 *              and the task is no longer needed by the engine.
 *
 *              The handle for the newly created task is returned in
 *              *task.  If the application does not need a task handle,
 *              task can be set to NULL and the engine will remove the
 *              task and call free_op_data as soon as it is no longer
 *              needed.  The handle returned in *task is ready to be used
 *              as soon as either this function returns or the task's op
 *              function is called.  Therefore it is safe, for example, to
 *              store the task handle in a field in op_data and pass the
 *              address of that field to this function.  It is also safe,
 *              in that situation, to call AXEfinish() within that task's
 *              op function.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
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

    /* If the caller requested a handle, pass the caller's pointer directly to
     * the internal functions, so the handle is available by the time the task
     * launches. */
    if(!task)
        task = &int_task;

    /* Create task */
    if(AXE_task_create(engine, task, num_necessary_parents, necessary_parents, num_sufficient_parents, sufficient_parents, op, op_data, free_op_data) != AXE_SUCCEED)
        ERROR;
    assert(*task);

    /* If the caller did not request a handle, decrement the reference count
     * because we will throw away our task pointer. */
    if(int_task) {
#ifdef AXE_DEBUG_REF
        printf("AXEcreate_task: decr ref: %p", int_task);
#endif /* AXE_DEBUG_REF */
        assert(*task == int_task);
        if(AXE_task_decr_ref(int_task, NULL) != AXE_SUCCEED)
            ERROR;
    } /* end if */
    else
        assert(*task != int_task);

done:
    return ret_value;
} /* end AXEcreate_task() */


/*-------------------------------------------------------------------------
 * Function:    AXEcreate_barrier_task
 *
 * Purpose:     Create a task in the engine identified by engine with a
 *              necessary parent dependency on all the current leaf nodes
 *              of the engine’s DAG.  In other words, this task serves as
 *              a synchronization point for all the current tasks in the
 *              engine’s DAG.  For the purposes of this function a "leaf
 *              node" is a node with no necessary children (it may have
 *              sufficient children).
 *
 *              If the op parameter is NULL, no operation will be invoked
 *              for this task and this task is solely a placeholder in the
 *              graph for other tasks to depend on.  If not set to NULL,
 *              the free_op_data callback will be invoked on the task’s
 *              op_data pointer when all the outstanding references to the
 *              new task are released (with AXEfinish*) and the task is no
 *              longer needed by the engine.
 *
 *              The handle for the newly created task is returned in
 *              *task.  If the application does not need a task handle,
 *              task can be set to NULL and the engine will remove the
 *              task and call free_op_data as soon as it is no longer
 *              needed.  The handle returned in *task is ready to be used
 *              as soon as either this function returns or the task's op
 *              function is called.  Therefore it is safe, for example, to
 *              store the task handle in a field in op_data and pass the
 *              address of that field to this function.  It is also safe,
 *              in that situation, to call AXEfinish() within that task's
 *              op function.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEcreate_barrier_task(AXE_engine_t engine, AXE_task_t *task/*out*/,
    AXE_task_op_t op, void *op_data, AXE_task_free_op_data_t free_op_data)
{
    AXE_task_int_t *int_task = NULL;
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!engine)
        ERROR;

    /* If the caller requested a handle, pass the caller's pointer directly to
     * the internal functions, so the handle is available by the time the task
     * launches. */
    if(!task)
        task = &int_task;

    /* Create barrier task */
    if(AXE_task_create_barrier(engine, task, op, op_data, free_op_data) != AXE_SUCCEED)
        ERROR;
    assert(*task);

    /* If the caller did not request a handle, decrement the reference count
     * because we will throw away our task pointer. */
    if(int_task) {
#ifdef AXE_DEBUG_REF
        printf("AXEcreate_barrier_task: decr ref: %p", int_task);
#endif /* AXE_DEBUG_REF */
        assert(*task == int_task);
        if(AXE_task_decr_ref(int_task, NULL) != AXE_SUCCEED)
            ERROR;
    } /* end if */
    else
        assert(*task != int_task);

done:
    return ret_value;
} /* end AXEcreate_barrier_task() */


/*-------------------------------------------------------------------------
 * Function:    AXEremove
 *
 * Purpose:     Attempt to remove a task from an engine.  Tasks that have
 *              any necessary or sufficient children may not be removed.
 *              The result of the attempt is returned in *remove_status.
 *              If the task has not started running, the task will be
 *              canceled and *remove_status will be set to AXE_CANCELED.
 *              If the task is running, the task will not be canceled and
 *              *remove_status will be set to AXE_NOT_CANCELED.  If the
 *              task has already finished, the task will not be canceled
 *              and *remove_status will be set to AXE_ALL_DONE.  If the
 *              task has any necessary or sufficient children this
 *              function will return AXE_FAIL and *remove_status will not
 *              be set.
 *
 *              This function does not release the task handle.
 *              AXEfinish* must still be called on task when the
 *              application is done with it.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEremove(AXE_task_t task, AXE_remove_status_t *remove_status)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!task)
        ERROR;

    /* Cancel task */
    if(AXE_task_cancel_leaf(task, remove_status) != AXE_SUCCEED)
        ERROR;

done:
    return ret_value;
} /* end AXEremove() */


/*-------------------------------------------------------------------------
 * Function:    AXEremove_all
 *
 * Purpose:     Attempt to remove all tasks from an engine.  The result of
 *              the attempt is returned in *remove_status.  If at least
 *              one task was canceled and all others (if any) were already
 *              complete remove_status will be set to AXE_CANCELED.  If at
 *              least one task was already running *remove_status will be
 *              set to AXE_NOT_CANCELED.  If all tasks had already
 *              finished *remove_status will be set to AXE_ALL_DONE.
 *
 *              This function does not release the task handles.
 *              AXEfinish* must still be called on the tasks when the
 *              application is done with them.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEremove_all(AXE_engine_t engine, AXE_remove_status_t *remove_status)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!engine)
        ERROR;

    /* Cancel all tasks */
    if(AXE_schedule_cancel_all(((AXE_engine_int_t *)engine)->schedule, remove_status) != AXE_SUCCEED)
        ERROR;

done:
    return ret_value;
} /* end AXEremove_all() */


/*-------------------------------------------------------------------------
 * Function:    AXEget_op_data
 *
 * Purpose:     Retrieves the op_data pointer associated with the
 *              specified task.  The op_data pointer is returned in
 *              *op_data.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
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


/*-------------------------------------------------------------------------
 * Function:    AXEget_status
 *
 * Purpose:     Retrieves the of the specified task, and can be used to
 *              determine if a task has completed.  The status is returned
 *              in *status.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
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


/*-------------------------------------------------------------------------
 * Function:    AXEwait
 *
 * Purpose:     Block program execution until the specified task completes
 *              execution.  If the task depends on other tasks in the
 *              engine, then those parent tasks will also have completed
 *              when this function returns.  If the task had completed
 *              before this function is called this funciton will
 *              immediately return AXE_SUCCEED.  If the task is canceled
 *              while waiting or was canceled before this function is
 *              called this function will return failure.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
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


/*-------------------------------------------------------------------------
 * Function:    AXEfinish
 *
 * Purpose:     Inform the engine that the application is finished with
 *              this task handle.  If all the handles for the task have
 *              been released and the engine no longer needs to use the
 *              task internally, all engine-side resources will be
 *              released and the task’s free_op_data callback will be
 *              invoked.  Use of task after calling this function will
 *              result in undefined behavior, unless another copy of this
 *              handle is held.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
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
    if(AXE_task_decr_ref(task, NULL) != AXE_SUCCEED)
        ERROR;

done:
    return ret_value;
} /* end AXEfinish() */


/*-------------------------------------------------------------------------
 * Function:    AXEfinish_all
 *
 * Purpose:     Inform the engine that the application is finished with
 *              a set of task handles.  If all the handles for a task have
 *              been released and the engine no longer needs to use the
 *              task internally, all engine-side resources will be
 *              released and the task’s free_op_data callback will be
 *              invoked.  Use of task after calling this function will
 *              result in undefined behavior, unless another copy of this
 *              handle is held.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEfinish_all(size_t num_tasks, AXE_task_t task[])
{
    size_t i;
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(num_tasks > 0 && !task)
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
        if(AXE_task_decr_ref(task[i], NULL) != AXE_SUCCEED)
            ret_value = AXE_FAIL;
    } /* end for */

done:
    return ret_value;
} /* end AXEfinish_all() */


/*-------------------------------------------------------------------------
 * Function:    AXEbegin_try
 *
 * Purpose:     Suspend error message printing by the library.  Functions
 *              will still return AXE_FAIL if they fail.  Error message
 *              printing may be resumed using AXEend_try().  If
 *              AXEbegin_try() is called multiple times, an equal number
 *              of calls to AXEend_try() will be necessary to resume error
 *              printing.  Calling AXEend_try() more times than
 *              AXEbegin_try() will result in undefined behavior.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEbegin_try(void)
{
    OPA_incr_int(&AXE_quiet_g);

    return AXE_SUCCEED;
} /* end AXE_begin_try() */


/*-------------------------------------------------------------------------
 * Function:    AXEbegin_try
 *
 * Purpose:     Resume error message printing by the library.  If
 *              AXEbegin_try() is called multiple times, an equal number
 *              of calls to AXEend_try() will be necessary to resume error
 *              printing.  Calling AXEend_try() more times than
 *              AXEbegin_try() will result in undefined behavior.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEend_try(void)
{
    OPA_decr_int(&AXE_quiet_g);

    return AXE_SUCCEED;
} /* end AXE_end_try() */

