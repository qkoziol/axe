/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "AXEengine.h"
#include "AXEid.h"
#include "AXEschedule.h"
#include "AXEtask.h"
#include "AXEthreadpool.h"


/*
 * Global variables
 */
OPA_int_t AXE_quiet_g = OPA_INT_T_INITIALIZER(0); /* Ref count for number of calls to AXE_begin_try() */
const AXE_engine_attr_t AXE_engine_attr_def_g = {AXE_THREAD_POOL_NUM_THREADS_DEF, AXE_ID_NUM_BUCKETS_DEF, AXE_ID_NUM_MUTEXES_DEF, AXE_ID_MIN_ID_DEF, AXE_ID_MAX_ID_DEF}; /* Default engine creation attributes */


/*-------------------------------------------------------------------------
 * Function:    AXEengine_attr_init
 *
 * Purpose:     Initialize the provided engine attribute, setting default
 *              values.  Must be called prior to use of the engine
 *              attribute.
 *
 *              Engine attributes are not currently meant to be used
 *              concurrently by multiple threads unless they are not
 *              modified while being used concurrently.  If the
 *              application wants to modify an attribute while it is being
 *              used by another thread, it is the application's
 *              responsibility to protect the attribute with a mutex.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              April 19, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEengine_attr_init(AXE_engine_attr_t *attr)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!attr)
        ERROR;

    memcpy(attr, &AXE_engine_attr_def_g, sizeof(*attr));

done:
    return ret_value;
} /* end AXEengine_attr_init() */


/*-------------------------------------------------------------------------
 * Function:    AXEengine_attr_destroy
 *
 * Purpose:     Destroys the engine attribute, freeing any memory used in
 *              any internal fields.  Currently does nothing, but may be
 *              necessary in a future version.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              April 19, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEengine_attr_destroy(AXE_engine_attr_t *attr)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!attr)
        ERROR;

    /* No-op for now */

done:
    return ret_value;
} /* end AXEengine_attr_destroy() */


/*-------------------------------------------------------------------------
 * Function:    AXEset_num_threads
 *
 * Purpose:     Sets the number of threads for the engine to use on the
 *              provided engine attribute.  An engine created using this
 *              attribute will always create and maintain num_threads
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
 *              The default value is 8.  This could be added as a
 *              configure option in a future version.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              April 19, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEset_num_threads(AXE_engine_attr_t *attr, size_t num_threads)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!attr)
        ERROR;
    if(num_threads == 0)
        ERROR;

    attr->num_threads = num_threads;

done:
    return ret_value;
} /* end AXEset_num_threads() */


/*-------------------------------------------------------------------------
 * Function:    AXEget_num_threads
 *
 * Purpose:     Gets the number of threads for the engine to use from the
 *              provided engine attribute.  An engine created using this
 *              attribute will always create and maintain num_threads
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
 *              The default value is 8.  This could be added as a
 *              configure option in a future version.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              April 19, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEget_num_threads(const AXE_engine_attr_t *attr, size_t *num_threads)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!attr)
        ERROR;

    if(num_threads)
        *num_threads = attr->num_threads;

done:
    return ret_value;
} /* end AXEget_num_id_threads() */


/*-------------------------------------------------------------------------
 * Function:    AXEset_id_range
 *
 * Purpose:     Sets the range of ids for automatic id generation on the
 *              specified engine attribute.  All ids generated by
 *              AXEgenerate_task_id() will fall between min_id and max_id
 *              (inclusive).  If all ids in the range are in use,
 *              AXEgenerate_task_id() will fail.
 *
 *              Care must be taken if mixing use of AXEgenerate_task_id()
 *              with manual assignment of task ids, as it is possible that
 *              AXEgenerate_task_id() could generate an id that the
 *              application thinks is free.  It is probably a good idea to
 *              manually assign ids from outside the range specified in
 *              this function.
 *
 *              The default values are 0 and UINT64_MAX.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              April 19, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEset_id_range(AXE_engine_attr_t *attr, AXE_task_t min_id, AXE_task_t max_id)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!attr)
        ERROR;
    if(min_id > max_id)
        ERROR;

    attr->min_id = min_id;
    attr->max_id = max_id;

done:
    return ret_value;
} /* end AXEset_id_range() */


/*-------------------------------------------------------------------------
 * Function:    AXEget_id_range
 *
 * Purpose:     Gets the range of ids for automatic id generation from the
 *              specified engine attribute.  All ids generated by
 *              AXEgenerate_task_id() will fall between min_id and max_id
 *              (inclusive).  If all ids in the range are in use,
 *              AXEgenerate_task_id() will fail.
 *
 *              Care must be taken if mixing use of AXEgenerate_task_id()
 *              with manual assignment of task ids, as it is possible that
 *              AXEgenerate_task_id() could generate an id that the
 *              application thinks is free.  It is probably a good idea to
 *              manually assign ids from outside the range specified in
 *              this function.
 *
 *              The default values are 0 and UINT64_MAX.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              April 19, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEget_id_range(const AXE_engine_attr_t *attr, AXE_task_t *min_id,
    AXE_task_t *max_id)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!attr)
        ERROR;

    if(min_id)
        *min_id = attr->min_id;
    if(max_id)
        *max_id = attr->max_id;

done:
    return ret_value;
} /* end AXEget_id_range() */


/*-------------------------------------------------------------------------
 * Function:    AXEset_num_id_buckets
 *
 * Purpose:     Sets the number of buckets in the engine's id hash table
 *              on the provided engine attribute.  The hash function is
 *              simply id % num_buckets.
 *
 *              Setting a larger value for num_buckets will reduce the
 *              number of hash value collisions, improving performance in
 *              most cases, but will make iteration more expensive,
 *              reducing performance for AXEcreate_engine(),
 *              AXEterminate_engine(), AXEcreate_barrier_task(), and
 *              AXEremove_all(), and will consume more memory.  Setting a
 *              smaller value for num_buckets will have the opposite
 *              effect.
 *
 *              The default value is 10007.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              April 19, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEset_num_id_buckets(AXE_engine_attr_t *attr, size_t num_buckets)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!attr)
        ERROR;
    if(num_buckets == 0)
        ERROR;

    attr->num_buckets = num_buckets;

done:
    return ret_value;
} /* end AXEset_num_id_buckets() */


/*-------------------------------------------------------------------------
 * Function:    AXEget_num_id_buckets
 *
 * Purpose:     Gets the number of buckets in the engine's id hash table
 *              from the provided engine attribute.  The hash function is
 *              simply id % num_buckets.
 *
 *              Setting a larger value for num_buckets will reduce the
 *              number of hash value collisions, improving performance in
 *              most cases, but will make iteration more expensive,
 *              reducing performance for AXEcreate_engine(),
 *              AXEterminate_engine(), AXEcreate_barrier_task(), and
 *              AXEremove_all(), and will consume more memory.  Setting a
 *              smaller value for num_buckets will have the opposite
 *              effect.
 *
 *              The default value is 10007.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              April 19, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEget_num_id_buckets(const AXE_engine_attr_t *attr, size_t *num_buckets)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!attr)
        ERROR;

    if(num_buckets)
        *num_buckets = attr->num_buckets;

done:
    return ret_value;
} /* end AXEget_num_id_buckets() */


/*-------------------------------------------------------------------------
 * Function:    AXEset_num_id_mutexes
 *
 * Purpose:     Sets the number of mutexes used to protect id hash buckets
 *              on the provided engine attribute.  Each hash bucket uses
 *              its mutex to prevent simultaneous to elements in the
 *              bucket (except in some cases where it is allowed).  The
 *              index of the mutex used by a hash bucket is given by
 *              bucket_index % num_mutexes.
 *
 *              Setting a larger value for num_mutexes will reduce
 *              contention for the mutexes, improving performance, but
 *              will consume more memory.  Setting a smaller value for
 *              num_mutexes will have the opposite effect.  There is no
 *              benefit to setting num_mutexes to a value larger than
 *              num_buckets.
 *
 *              The default value is 503.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              April 19, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEset_num_id_mutexes(AXE_engine_attr_t *attr, size_t num_mutexes)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!attr)
        ERROR;
    if(num_mutexes == 0)
        ERROR;

    attr->num_mutexes = num_mutexes;

done:
    return ret_value;
} /* end AXEset_num_id_mutexes() */


/*-------------------------------------------------------------------------
 * Function:    AXEget_num_id_mutexes
 *
 * Purpose:     Gets the number of mutexes used to protect id hash buckets
 *              from the provided engine attribute.  Each hash bucket uses
 *              its mutex to prevent simultaneous to elements in the
 *              bucket (except in some cases where it is allowed).  The
 *              index of the mutex used by a hash bucket is given by
 *              bucket_index % num_mutexes.
 *
 *              Setting a larger value for num_mutexes will reduce
 *              contention for the mutexes, improving performance, but
 *              will consume more memory.  Setting a smaller value for
 *              num_mutexes will have the opposite effect.  There is no
 *              benefit to setting num_mutexes to a value larger than
 *              num_buckets.
 *
 *              The default value is 503.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              April 19, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEget_num_id_mutexes(const AXE_engine_attr_t *attr, size_t *num_mutexes)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!attr)
        ERROR;

    if(num_mutexes)
        *num_mutexes = attr->num_mutexes;

done:
    return ret_value;
} /* end AXEget_num_id_mutexes() */


/*-------------------------------------------------------------------------
 * Function:    AXEcreate_engine
 *
 * Purpose:     Create a new engine for asynchronous execution, with the
 *              attributes for the engine specified by attr (if present)
 *              or default (if NULL).  The handle for the newly created
 *              engine is returned in *engine.
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
AXEcreate_engine(AXE_engine_t *engine/*out*/, const AXE_engine_attr_t *attr)
{
    AXE_engine_int_t *int_engine = NULL;
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!engine)
        ERROR;

    if(AXE_engine_create(&int_engine, attr ? attr : &AXE_engine_attr_def_g) != AXE_SUCCEED)
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
 * Function:    AXEgenerate_task_id
 *
 * Purpose:     Generate a task id that can be used in a subsequent call
 *              to AXEcreate_task() or AXEcreate_barrier_task().  The id
 *              will be between the min_id and max_id parameters given to
 *              AXEengine_attr_id_range().  The id is returned in *task.
 *              The id must never be reused, even if AXEfinish() or
 *              AXEfinish_all() is called on the task, unless it is
 *              returned by another call to AXEgenerate_task_id().  If the
 *              id is no longer needed and has not been used it can be
 *              released with AXErelease_task_id().
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              April 16, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXEgenerate_task_id(AXE_engine_t engine, AXE_task_t *task)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!engine)
        ERROR;
    if(!task)
        ERROR;

    /* Generate task id */
    if(AXE_id_generate(engine->id_table, (AXE_id_t *)task) != AXE_SUCCEED)
        ERROR;

done:
    return ret_value;
} /* end AXEgenerate_task_id() */


/*-------------------------------------------------------------------------
 * Function:    AXErelease_task_id
 *
 * Purpose:     Release the specified task id back to the engine, allowing
 *              it to be reused.  The task must have been previously
 *              generated by AXEgenerate_task_id(), and must not have been
 *              used to create a task.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              April 16, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXErelease_task_id(AXE_engine_t engine, AXE_task_t task)
{
    void *obj;
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!engine)
        ERROR;

    /* Lookup the id, to make sure it is present and has not been
     * associated with a task */
    if(AXE_id_lookup(engine->id_table, task, &obj) != AXE_SUCCEED)
        ERROR;
    if(obj)
        ERROR;

    /* Release task id */
    if(AXE_id_remove(engine->id_table, task) != AXE_SUCCEED)
        ERROR;

done:
    return ret_value;
} /* end AXErelease_task_id() */


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
 *              The task id is provided by the application in the task
 *              parameter.  This can be generated by the application or by
 *              AXEgenerate_task_id().  Applications must use caution when
 *              mixing both methods for id creation to make sure the same
 *              id is not used twice.  In this case, it may be prudent to
 *              define a range for automatic id generation using
 *              AXEengine_attr_id_range() and manually generate id outside
 *              that range.  task must not be reused, even if this
 *              function fails.
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
AXEcreate_task(AXE_engine_t engine, AXE_task_t task,
    size_t num_necessary_parents, AXE_task_t necessary_parents[],
    size_t num_sufficient_parents, AXE_task_t sufficient_parents[],
    AXE_task_op_t op, void *op_data, AXE_task_free_op_data_t free_op_data)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!engine)
        ERROR;
    if(num_necessary_parents > 0 && !necessary_parents)
        ERROR;
    if(num_sufficient_parents > 0 && !sufficient_parents)
        ERROR;

    /* Create task */
    if(AXE_task_create(engine, (AXE_id_t)task, num_necessary_parents, necessary_parents, num_sufficient_parents, sufficient_parents, op, op_data, free_op_data) != AXE_SUCCEED)
        ERROR;

done:
    return ret_value;
} /* end AXEcreate_task() */


/*-------------------------------------------------------------------------
 * Function:    AXEcreate_barrier_task
 *
 * Purpose:     fCreate a task in the engine identified by engine with a
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
 *              The task id is provided by the application in the task
 *              parameter.  This can be generated by the application or by
 *              AXEgenerate_task_id().  Applications must use caution when
 *              mixing both methods for id creation to make sure the same
 *              id is not used twice.  In this case, it may be prudent to
 *              define a range for automatic id generation using
 *              AXEengine_attr_id_range() and manually generate id outside
 *              that range.  task must not be reused, even if this
 *              function fails.
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
AXEcreate_barrier_task(AXE_engine_t engine, AXE_task_t task, AXE_task_op_t op,
    void *op_data, AXE_task_free_op_data_t free_op_data)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!engine)
        ERROR;

    /* Create barrier task */
    if(AXE_task_create_barrier(engine, (AXE_id_t)task, op, op_data, free_op_data) != AXE_SUCCEED)
        ERROR;

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
AXEremove(AXE_engine_t engine, AXE_task_t task,
    AXE_remove_status_t *remove_status)
{
    AXE_task_int_t *int_task = NULL;
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Look up task */
    if(AXE_id_lookup(engine->id_table, task, (void **)(void *)&int_task) != AXE_SUCCEED)
        ERROR;

    /* Check if task is NULL (indicates id was generated but task was never
     * created) */
    if(!int_task)
        ERROR;

    /* Cancel task */
    if(AXE_task_cancel_leaf(int_task, remove_status) != AXE_SUCCEED)
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
    if(AXE_schedule_cancel_all(((AXE_engine_int_t *)engine)->schedule, ((AXE_engine_int_t *)engine)->id_table, remove_status) != AXE_SUCCEED)
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
AXEget_op_data(AXE_engine_t engine, AXE_task_t task, void **op_data/*out*/)
{
    AXE_task_int_t *int_task = NULL;
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!op_data)
        ERROR;

    /* Look up task */
    if(AXE_id_lookup(engine->id_table, task, (void **)(void *)&int_task) != AXE_SUCCEED)
        ERROR;

    /* Check if task is NULL (indicates id was generated but task was never
     * created) */
    if(!int_task)
        ERROR;

    /* Get op data */
    AXE_task_get_op_data(int_task, op_data);

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
AXEget_status(AXE_engine_t engine, AXE_task_t task, AXE_status_t *status/*out*/)
{
    AXE_task_int_t *int_task = NULL;
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(!status)
        ERROR;

    /* Look up task */
    if(AXE_id_lookup(engine->id_table, task, (void **)(void *)&int_task) != AXE_SUCCEED)
        ERROR;

    /* Check if task is NULL (indicates id was generated but task was never
     * created) */
    if(!int_task)
        ERROR;

    /* Use read/write barriers before and after we retrieve the status, in case
     * the application is using the result to determine a course of action that
     * is only valid for a certain status.  */
    OPA_read_write_barrier();

    /* Get op data */
    AXE_task_get_status(int_task, status);

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
 *              before this function is called this function will
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
AXEwait(AXE_engine_t engine, AXE_task_t task)
{
    AXE_task_int_t *int_task = NULL;
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Look up task */
    if(AXE_id_lookup(engine->id_table, task, (void **)(void *)&int_task) != AXE_SUCCEED)
        ERROR;

    /* Check if task is NULL (indicates id was generated but task was never
     * created) */
    if(!int_task)
        ERROR;

    /* Wait for task to complete */
    if(AXE_task_wait(int_task) != AXE_SUCCEED)
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
AXEfinish(AXE_engine_t engine, AXE_task_t task)
{
    AXE_task_int_t *int_task = NULL;
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Look up task */
    if(AXE_id_lookup(engine->id_table, task, (void **)(void *)&int_task) != AXE_SUCCEED)
        ERROR;

    /* Check if task is NULL (indicates id was generated but task was never
     * created) */
    if(!int_task)
        ERROR;

    /* Decrement reference count on task, it will be freed if it drops to zero
     */
#ifdef AXE_DEBUG_REF
    printf("AXEfinish: decr ref: %p", task);
#endif /* AXE_DEBUG_REF */
    if(AXE_task_decr_ref(int_task, NULL) != AXE_SUCCEED)
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
AXEfinish_all(AXE_engine_t engine, size_t num_tasks, AXE_task_t task[])
{
    AXE_task_int_t *int_task = NULL;
    size_t i;
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Check parameters */
    if(num_tasks > 0 && !task)
        ERROR;

    /* Iterate over all tasks */
    for(i = 0; i < num_tasks; i++) {
        /* Look up task */
        if(AXE_id_lookup(engine->id_table, task[i], (void **)(void *)&int_task) != AXE_SUCCEED)
            ERROR;

        /* Check if task is NULL (indicates id was generated but task was never
         * created) */
        if(!int_task)
            ERROR;

        /* Decrement reference count on task, it will be freed if it drops to zero
         */
#ifdef AXE_DEBUG_REF
    printf("AXEfinish_all: decr ref: %p", task);
#endif /* AXE_DEBUG_REF */
        if(AXE_task_decr_ref(int_task, NULL) != AXE_SUCCEED)
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

