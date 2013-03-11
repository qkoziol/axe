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


/*
 * Local functions
 */
static AXE_error_t AXE_task_init(AXE_engine_int_t *engine,
    AXE_task_int_t **task/*out*/, AXE_task_op_t op, void *op_data,
    AXE_task_free_op_data_t free_op_data);


void
AXE_task_incr_ref(AXE_task_int_t *task)
{
#ifdef AXE_DEBUG_REF
    printf(" %d\n", OPA_fetch_and_incr_int(&task->rc) + 1); fflush(stdout);
#else /* AXE_DEBUG_REF */
    OPA_incr_int(&task->rc);
#endif /* AXE_DEBUG_REF */

    return;
} /* end AXE_task_incr_ref() */


void
AXE_task_decr_ref(AXE_task_int_t *task, AXE_task_int_t **free_ptr)
{
#ifdef AXE_DEBUG_REF
    int rc = OPA_fetch_and_decr_int(&task->rc) - 1;

    printf(" %d\n", rc); fflush(stdout);
    if(rc == 0) {
#else /* AXE_DEBUG_REF */
    if(OPA_decr_and_test_int(&task->rc)) {
#endif /* AXE_DEBUG_REF */
        /* The scheduler should always hold a reference until the task is done
         * (until we implement remove, etc.) */
        assert(((AXE_status_t)OPA_load_int(&task->status) == AXE_TASK_DONE) || ((AXE_status_t)OPA_load_int(&task->status) == AXE_TASK_CANCELED));

        /* If we were provided a free pointer, set it to point to the task.
         * Otherwise, free the task */
        if(free_ptr)
            *free_ptr = task;
        else
            AXE_task_free(task);
    } /* end if */

    return;
} /* end AXE_task_decr_ref() */


AXE_error_t
AXE_task_create(AXE_engine_int_t *engine, AXE_task_int_t **task/*out*/,
    size_t num_necessary_parents, AXE_task_int_t **necessary_parents,
    size_t num_sufficient_parents, AXE_task_int_t **sufficient_parents,
    AXE_task_op_t op, void *op_data, AXE_task_free_op_data_t free_op_data)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(engine);
    assert(task);
    assert(num_necessary_parents == 0 || necessary_parents);
    assert(num_sufficient_parents == 0 || sufficient_parents);

    *task = NULL;

    /* Allocate and initialize task struct */
    if(AXE_task_init(engine, task, op, op_data, free_op_data) != AXE_SUCCEED)
        ERROR;

    /* Copy necessary and sufficient parent arrays */
    if(num_necessary_parents) {
        if(NULL == ((*task)->necessary_parents = (AXE_task_int_t **)malloc(num_necessary_parents * sizeof(AXE_task_int_t *))))
            ERROR;
        (void)memcpy((*task)->necessary_parents, necessary_parents, num_necessary_parents * sizeof(AXE_task_int_t *));
    } /* end if */
    (*task)->num_necessary_parents = num_necessary_parents;
    if(num_sufficient_parents) {
        if(NULL == ((*task)->sufficient_parents = (AXE_task_int_t **)malloc(num_sufficient_parents * sizeof(AXE_task_int_t *))))
            ERROR;
        (void)memcpy((*task)->sufficient_parents, sufficient_parents, num_sufficient_parents * sizeof(AXE_task_int_t *));
    } /* end if */
    (*task)->num_sufficient_parents = num_sufficient_parents;

    /* Check if the sufficient condition is already complete, due to the task
     * having no sufficient parents */
    OPA_store_int(&(*task)->sufficient_complete, (int)(num_sufficient_parents == 0));

    /* Initialize num_necessary_complete - includes one for sufficient_complete
     */
    OPA_store_int(&(*task)->num_conditions_complete, OPA_load_int(&(*task)->sufficient_complete));

    /* Add task to schedule */
    if(AXE_schedule_add(*task) != AXE_SUCCEED)
        ERROR;

done:
    if(ret_value == AXE_FAIL)
        if(*task)
            AXE_task_decr_ref(*task, NULL);

    return ret_value;
} /* end AXE_task_create() */


AXE_error_t
AXE_task_create_barrier(AXE_engine_int_t *engine, AXE_task_int_t **task/*out*/,
    AXE_task_op_t op, void *op_data, AXE_task_free_op_data_t free_op_data)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(engine);
    assert(task);

    *task = NULL;

    /* Allocate and initialize task struct */
    if(AXE_task_init(engine, task, op, op_data, free_op_data) != AXE_SUCCEED)
        ERROR;

    /* Initialize sufficient_complete to TRUE and num_conditions_complete to 1,
     * as barrier tasks never have sufficient parents */
    OPA_store_int(&(*task)->sufficient_complete, TRUE);
    OPA_store_int(&(*task)->num_conditions_complete, 1);

    /* Add barrier task to schedule */
    if(AXE_schedule_add_barrier(*task) != AXE_SUCCEED)
        ERROR;

done:
    if(ret_value == AXE_FAIL)
        if(*task)
            AXE_task_decr_ref(*task, NULL);

    return ret_value;
} /* end AXE_task_create_barrier() */


void
AXE_task_get_op_data(AXE_task_int_t *task, void **op_data/*out*/)
{
    assert(task);
    assert(op_data);

    /* Get op data */
    *op_data = task->op_data;

    return;
} /* end AXE_task_get_op_data() */


void
AXE_task_get_status(AXE_task_int_t *task, AXE_status_t *status/*out*/)
{
    assert(task);
    assert(status);

    /* Get status */
    *status = (AXE_status_t)OPA_load_int(&task->status);

    return;
} /* end AXE_task_get_op_data() */


AXE_error_t
AXE_task_worker(void *_task)
{
    AXE_task_int_t *task = (AXE_task_int_t *)_task;
    size_t old_num_sufficient_parents;
    size_t i;
    size_t block;
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(task);

    /* Let the scheduler know that this worker is running and will eventually
     * check the schedule for more tasks */
    AXE_schedule_worker_running(task->engine->schedule);

    /* Main loop */
    do {
        /* Mark task as running, if it has not been canceled */
        if((AXE_status_t)OPA_cas_int(&task->status, (int)AXE_TASK_SCHEDULED,
                (int)AXE_TASK_RUNNING) == AXE_TASK_SCHEDULED) {

            /* Update sufficient parents array */
            /* No need to worry about contention since no other threads should
             * modify this array */
            old_num_sufficient_parents = task->num_sufficient_parents;
            task->num_sufficient_parents = 0;
            block = 0;
            for(i = 0; i < old_num_sufficient_parents; i++) {
                /* Keep track of how many complete tasks we found in a row and
                 * copy in blocks */
                if((AXE_status_t)OPA_load_int(&task->sufficient_parents[i]->status)
                        == AXE_TASK_DONE)
                    block++;
                else {
                    /* Decrement reference count on uncomplete parent */
    #ifdef AXE_DEBUG_REF
                    printf("AXE_task_worker: decr ref: %p\n", task->sufficient_parents[i]);
    #endif /* AXE_DEBUG_REF */
                    AXE_task_decr_ref(task->sufficient_parents[i], NULL);

                    if(block) {
                        /* End of block, slide block down (if necessary) */
                        if(block != i)
                            (void)memmove(&task->sufficient_parents[task->num_sufficient_parents], &task->sufficient_parents[i - block], block * sizeof(task->sufficient_parents[0]));

                        /* Update number of sufficient parents and reset block
                         */
                        task->num_sufficient_parents += block;
                        block = 0;
                    } /* end if */
                } /* end if */
            } /* end for */

            /* Handle block at end, if any */
            if(block) {
                /* Slide block down (if necessary) */
                if(block != i)
                    (void)memmove(&task->sufficient_parents[task->num_sufficient_parents], &task->sufficient_parents[i - block], block * sizeof(task->sufficient_parents[0]));

                /* Update number of sufficient parents */
                task->num_sufficient_parents += block;
            } /* end if */

            assert(task->num_sufficient_parents <= old_num_sufficient_parents);

            /* Execute client task */
            if(task->op)
                (task->op)(task->num_necessary_parents, (AXE_task_t *)task->necessary_parents, task->num_sufficient_parents, (AXE_task_t *)task->sufficient_parents, task->op_data);
        } /* end if */
        else {
            /* The task is canceled */
            assert((AXE_status_t)OPA_load_int(&task->status) == AXE_TASK_CANCELED);

            /* Remove references to all necessary parents */
            for(i = 0; i < task->num_necessary_parents; i++)
                AXE_task_decr_ref(task->necessary_parents[i], NULL);

            /* Remove references to all sufficient parents */
            for(i = 0; i < task->num_sufficient_parents; i++)
                AXE_task_decr_ref(task->sufficient_parents[i], NULL);
        } /* end else */

        /* Update the schedule to reflect that this task is complete, and
         * retrieve new task (AXE_schedule_finish takes ownership of old task)
         */
        if(AXE_schedule_finish(&task) != AXE_SUCCEED)
            ERROR;
    } while(task);

done:
    return ret_value;
} /* end AXE_task_worker() */


AXE_error_t
AXE_task_wait(AXE_task_int_t *task)
{
    _Bool is_mutex_locked = TRUE;
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(task);

    /* Lock task mutex.  Do so before checking the status so we know that
     * (together with the similar mutex in AXE_schedule_finish()) if the status
     * is not AXE_TASK_DONE that we will be woken up from pthread_cond_wait()
     * when the task is complete, i.e. the signal will not be sent before this
     * thread begins waiting. */
#ifdef AXE_DEBUG_LOCK
    printf("AXE_task_wait: lock task_mutex: %p\n", &task->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
    if(0 != pthread_mutex_lock(&task->task_mutex))
        ERROR;
    is_mutex_locked = TRUE;

    /* Check if the task is already complete (or canceled) */
    if(((AXE_status_t)OPA_load_int(&task->status) != AXE_TASK_DONE)
            && ((AXE_status_t)OPA_load_int(&task->status) != AXE_TASK_CANCELED))
        /* Wait for signal */
        if(0 != pthread_cond_wait(&task->wait_cond, &task->task_mutex))
            ERROR;

    if((AXE_status_t)OPA_load_int(&task->status) != AXE_TASK_DONE) {
        assert((AXE_status_t)OPA_load_int(&task->status) == AXE_TASK_CANCELED);
        ERROR;
    } /* end if */

done:
    /* Unlock wait mutex */
    if(is_mutex_locked) {
#ifdef AXE_DEBUG_LOCK
        printf("AXE_task_wait: unlock task_mutex: %p\n", &task->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
        if(0 != pthread_mutex_unlock(&task->task_mutex))
            ERROR;
    } /* end if */

    return ret_value;
} /* end AXE_task_wait() */


AXE_error_t
AXE_task_cancel_leaf(AXE_task_int_t *task, AXE_remove_status_t *remove_status)
{
    _Bool is_mutex_locked = TRUE;
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(task);

    /* Lock task mutex */
#ifdef AXE_DEBUG_LOCK
    printf("AXE_task_cancel_leaf: lock task_mutex: %p\n", &task->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
    if(0 != pthread_mutex_lock(&task->task_mutex))
        ERROR;
    is_mutex_locked = TRUE;

    /* Make sure the task does not have any children */
    if((task->num_necessary_children != 0 )
            || (task->num_sufficient_children != 0))
        ERROR;

    /* Cancel the task if it is not running, complete, or already canceled */
    /* AXE_schedule_cancel will unlock the mutex */
    is_mutex_locked = FALSE;
    if(AXE_schedule_cancel(task, remove_status, TRUE) != AXE_SUCCEED)
        ERROR;

done:
    /* Unlock task mutex */
    if(is_mutex_locked) {
        assert(ret_value == AXE_FAIL);
#ifdef AXE_DEBUG_LOCK
        printf("AXE_task_cancel_leaf: unlock task_mutex: %p\n", &task->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
        (void)pthread_mutex_unlock(&task->task_mutex);
    } /* end if */

    return ret_value;
} /* end AXE_task_cancel_leaf() */


AXE_error_t
AXE_task_free(AXE_task_int_t *task)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(task);
    assert(((AXE_status_t)OPA_load_int(&task->status) == AXE_TASK_DONE) || ((AXE_status_t)OPA_load_int(&task->status) == AXE_TASK_CANCELED));

#ifdef AXE_DEBUG
    printf("AXE_task_free: %p\n", task); fflush(stdout);
#endif /* AXE_DEBUG */

    /* Remove from task list, if in list (might not be in list because the
     * schedule is being freed) */
    if(task->task_list_next)
        if(AXE_schedule_remove_from_list(task) != AXE_SUCCEED)
            ERROR;

    /* Free fields */
    if(task->necessary_parents)
        free(task->necessary_parents);
    if(task->sufficient_parents)
        free(task->sufficient_parents);
    if(task->free_op_data)
        (task->free_op_data)(task->op_data);
    (void)pthread_mutex_destroy(&task->task_mutex);
    (void)pthread_cond_destroy(&task->wait_cond);
    if(task->necessary_children)
        free(task->necessary_children);
    if(task->sufficient_children)
        free(task->sufficient_children);

    /* Free task struct */
#ifndef NDEBUG
    memset(task, 0, sizeof(*task));
#endif /* NDEBUG */
    free(task);

done:
    return ret_value;
} /* end AXE_task_free */


static AXE_error_t
AXE_task_init(AXE_engine_int_t *engine, AXE_task_int_t **task/*out*/,
    AXE_task_op_t op, void *op_data, AXE_task_free_op_data_t free_op_data)
{
    _Bool is_task_mutex_init = FALSE;
    _Bool is_wait_cond_init = FALSE;
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(engine);
    assert(task);

    *task = NULL;

    /* Allocate task struct */
    if(NULL == (*task = (AXE_task_int_t *)malloc(sizeof(AXE_task_int_t))))
        ERROR;

    /*
     * Initialize fields
     */
    (*task)->op = op;
    (*task)->num_necessary_parents = 0;
    (*task)->necessary_parents = NULL;
    (*task)->num_sufficient_parents = 0;
    (*task)->sufficient_parents = NULL;
    (*task)->op_data = op_data;
    OPA_Queue_header_init(&(*task)->scheduled_queue_hdr);
    (*task)->engine = engine;
    (*task)->free_op_data = free_op_data;
    if(0 != pthread_mutex_init(&(*task)->task_mutex, NULL))
        ERROR;
    is_task_mutex_init = TRUE;
    if(0 != pthread_cond_init(&(*task)->wait_cond, NULL))
        ERROR;
    is_wait_cond_init = TRUE;
    OPA_store_int(&(*task)->status, (int)AXE_WAITING_FOR_PARENT);

    /* Initialize reference count to 1.  The caller of this function is
     * responsible for decrementing the reference count when it is done with the
     * reference this function returns. */
    OPA_store_int(&(*task)->rc, 1);

    /* Caller will initialize sufficient_complete and num_conditions_complete */
    (*task)->num_necessary_children = 0;
    (*task)->necessary_children_nalloc = 0;
    (*task)->necessary_children = NULL;
    (*task)->num_sufficient_children = 0;
    (*task)->sufficient_children_nalloc = 0;
    (*task)->sufficient_children = NULL;

    /* Schedule package will initialize task_list_next and task_list_prev */
    (*task)->free_list_next = NULL;

done:
    if(ret_value == AXE_FAIL)
        if(*task) {
            if(is_task_mutex_init)
                (void)pthread_mutex_destroy(&(*task)->task_mutex);
            if(is_wait_cond_init)
                (void)pthread_cond_destroy(&(*task)->wait_cond);
            free(*task);
            *task = NULL;
        } /* end if */

    return ret_value;
} /* end AXE_task_init() */

