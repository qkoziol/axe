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


void
AE2_task_incr_ref(AE2_task_int_t *task)
{
#ifdef NAF_DEBUG_REF
    printf(" %d\n", OPA_fetch_and_incr_int(&task->rc) + 1); fflush(stdout);
#else /* NAF_DEBUG_REF */
    OPA_incr_int(&task->rc);
#endif /* NAF_DEBUG_REF */

    return;
} /* end AE2_task_incr_ref() */


void
AE2_task_decr_ref(AE2_task_int_t *task)
{
#ifdef NAF_DEBUG_REF
    int rc = OPA_fetch_and_decr_int(&task->rc) - 1;

    printf(" %d\n", rc); fflush(stdout);
    if(rc == 0) {
#else /* NAF_DEBUG_REF */
    if(OPA_decr_and_test_int(&task->rc)) {
#endif /* NAF_DEBUG_REF */
        /* The scheduler should always hold a reference until the task is done
         * (until we implement remove, etc.) */
        assert((AE2_status_t)OPA_load_int(&task->status) == AE2_TASK_DONE);

        AE2_task_free(task);
    } /* end if */

    return;
} /* end AE2_task_decr_ref() */


AE2_error_t
AE2_task_create(AE2_engine_int_t *engine, AE2_task_int_t **task/*out*/,
    size_t num_necessary_parents, AE2_task_int_t **necessary_parents,
    size_t num_sufficient_parents, AE2_task_int_t **sufficient_parents,
    AE2_task_op_t op, void *op_data, AE2_task_free_op_data_t free_op_data)
{
    _Bool is_task_mutex_init = FALSE;
    _Bool is_wait_cond_init = FALSE;
    _Bool is_wait_mutex_init = FALSE;
    AE2_error_t ret_value = AE2_SUCCEED;

    assert(engine);
    assert(task);
    assert(num_necessary_parents == 0 || necessary_parents);
    assert(num_sufficient_parents == 0 || sufficient_parents);

    *task = NULL;

    /* Allocate task struct */
    if(NULL == (*task = (AE2_task_int_t *)malloc(sizeof(AE2_task_int_t))))
        ERROR;

    /* Initialize malloc'd fields to NULL, so they are not freed if something
     * goes wrong */
    (*task)->necessary_parents = NULL;
    (*task)->sufficient_parents = NULL;

    /*
     * Initialize fields
     */
    (*task)->op = op;
    (*task)->num_necessary_parents = num_necessary_parents;
    if(num_necessary_parents) {
        if(NULL == ((*task)->necessary_parents = (AE2_task_int_t **)malloc(num_necessary_parents * sizeof(AE2_task_int_t *))))
            ERROR;
        (void)memcpy((*task)->necessary_parents, necessary_parents, num_necessary_parents * sizeof(AE2_task_int_t *));
    } /* end if */
    (*task)->num_sufficient_parents = num_sufficient_parents;
    if(num_sufficient_parents) {
        if(NULL == ((*task)->sufficient_parents = (AE2_task_int_t **)malloc(num_sufficient_parents * sizeof(AE2_task_int_t *))))
            ERROR;
        (void)memcpy((*task)->sufficient_parents, sufficient_parents, num_sufficient_parents * sizeof(AE2_task_int_t *));
    } /* end if */
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
    if(0 != pthread_mutex_init(&(*task)->wait_mutex, NULL))
        ERROR;
    is_wait_mutex_init = TRUE;
    OPA_store_int(&(*task)->status, (int)AE2_WAITING_FOR_PARENT);

    /* Initialize reference count to 1.  The caller of this function is
     * responsible for decrementing the reference count when it is done with the
     * reference this function returns. */
    OPA_store_int(&(*task)->rc, 1);

    /* Check if the sufficient condition is already complete, due to the task
     * having no sufficient parents */
    OPA_store_int(&(*task)->sufficient_complete, (int)(num_sufficient_parents == 0));

    /* num_necessary_complete includes one for sufficient_complete */
    OPA_store_int(&(*task)->num_conditions_complete, OPA_load_int(&(*task)->sufficient_complete));
    (*task)->num_necessary_children = 0;
    (*task)->necessary_children_nalloc = 0;
    (*task)->necessary_children = NULL;
    (*task)->num_sufficient_children = 0;
    (*task)->sufficient_children_nalloc = 0;
    (*task)->sufficient_children = NULL;

    /* AE2_schedule_add will initialize task_list_next and task_list_prev */

    /* Add task to schedule */
    if(AE2_schedule_add(*task) != AE2_SUCCEED)
        ERROR;

done:
    if(ret_value == AE2_FAIL)
        if(*task) {
            if((*task)->necessary_parents)
                free((*task)->necessary_parents);
            if((*task)->sufficient_parents)
                free((*task)->sufficient_parents);
            if(is_task_mutex_init)
                (void)pthread_mutex_destroy(&(*task)->task_mutex);
            if(is_wait_cond_init)
                (void)pthread_cond_destroy(&(*task)->wait_cond);
            if(is_wait_mutex_init)
                (void)pthread_mutex_destroy(&(*task)->wait_mutex);
            free(*task);
            *task = NULL;
        } /* end if */

    return ret_value;
} /* end AE2_task_create() */


void
AE2_task_get_op_data(AE2_task_int_t *task, void **op_data/*out*/)
{
    assert(task);
    assert(op_data);

    /* Get op data */
    *op_data = task->op_data;

    return;
} /* end AE2_task_get_op_data() */


void
AE2_task_get_status(AE2_task_int_t *task, AE2_status_t *status/*out*/)
{
    assert(task);
    assert(status);

    /* Get status */
    *status = (AE2_status_t)OPA_load_int(&task->status);

    return;
} /* end AE2_task_get_op_data() */


AE2_error_t
AE2_task_worker(void *_task)
{
    AE2_task_int_t *task = (AE2_task_int_t *)_task;
    size_t old_num_sufficient_parents;
    size_t i;
    size_t block;
    AE2_error_t ret_value = AE2_SUCCEED;

    assert(task);
    assert(task->op);

    /* Let the scheduler know that this worker is running and will eventually
     * check the schedule for more tasks */
    AE2_schedule_worker_running(task->engine->schedule);

    /* Main loop */
    do {
        /* Mark task as running, if it has not been canceled */
        if((AE2_status_t)OPA_cas_int(&task->status, (int)AE2_TASK_SCHEDULED,
                (int)AE2_TASK_RUNNING) == AE2_TASK_SCHEDULED) {

            /* Update sufficient parents array */
            /* No need to worry about contention since no other threads should
             * modify this array */
            old_num_sufficient_parents = task->num_sufficient_parents;
            task->num_sufficient_parents = 0;
            block = 0;
            for(i = 0; i < old_num_sufficient_parents; i++) {
                /* Keep track of how many complete tasks we found in a row and
                 * copy in blocks */
                if((AE2_status_t)OPA_load_int(&task->sufficient_parents[i]->status)
                        == AE2_TASK_DONE)
                    block++;
                else {
                    /* Decrement reference count on uncomplete parent */
    #ifdef NAF_DEBUG_REF
                    printf("AE2_task_worker: decr ref: %p", task->sufficient_parents[i]);
    #endif /* NAF_DEBUG_REF */
                    AE2_task_decr_ref(task->sufficient_parents[i]);

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
                (task->op)(task->num_necessary_parents, (AE2_task_t *)task->necessary_parents, task->num_sufficient_parents, (AE2_task_t *)task->sufficient_parents, task->op_data);
        } /* end if */
        else {
            /* The task is canceled */
            assert((AE2_status_t)OPA_load_int(&task->status) == AE2_TASK_CANCELED);

            /* Remove references to all necessary parents */
            for(i = 0; i < task->num_necessary_parents; i++)
                AE2_task_decr_ref(task->necessary_parents[i]);

            /* Remove references to all sufficient parents */
            for(i = 0; i < task->num_sufficient_parents; i++)
                AE2_task_decr_ref(task->sufficient_parents[i]);
        } /* end else */

        /* Update the schedule to reflect that this task is complete, and
         * retrieve new task (AE2_schedule_finish takes ownership of old task)
         */
        if(AE2_schedule_finish(&task) != AE2_SUCCEED)
            ERROR;
    } while(task);

done:
    return ret_value;
} /* end AE2_task_worker() */


AE2_error_t
AE2_task_wait(AE2_task_int_t *task)
{
    AE2_error_t ret_value = AE2_SUCCEED;

    assert(task);

    /* Lock wait mutex.  Do so before checking the status so we know that
     * (together with the similar mutex in AE2_schedule_finish()) if the status
     * is not AE2_TASK_DONE that we will be woken up from pthread_cond_wait()
     * when the task is complete, i.e. the signal will not be sent before this
     * thread begins waiting. */
    if(0 != pthread_mutex_lock(&task->wait_mutex))
        ERROR;

    /* Check if the task is already complete */
    if((AE2_status_t)OPA_load_int(&task->status) != AE2_TASK_DONE)
        /* Wait for signal */
        if(0 != pthread_cond_wait(&task->wait_cond, &task->wait_mutex))
            ERROR;

    /* Unlock wait mutex */
    if(0 != pthread_mutex_unlock(&task->wait_mutex))
        ERROR;

    assert((AE2_status_t)OPA_load_int(&task->status) == AE2_TASK_DONE);

done:
    return ret_value;
} /* end AE2_task_wait() */


AE2_error_t
AE2_task_free(AE2_task_int_t *task)
{
    AE2_error_t ret_value = AE2_SUCCEED;

    assert(task);

#ifdef NAF_DEBUG
    printf("AE2_task_free: %p\n", task); fflush(stdout);
#endif /* NAF_DEBUG */

    /* Remove from task list, if in list (might not be in list because the
     * schedule is being freed) */
    if(task->task_list_next)
        if(AE2_schedule_remove_task(task) != AE2_SUCCEED)
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
    (void)pthread_mutex_destroy(&task->wait_mutex);
    if(task->necessary_children)
        free(task->necessary_children);
    if(task->sufficient_children)
        free(task->sufficient_children);

    /* Free task struct */
    free(task);

done:
    return ret_value;
} /* end AE2_task_free */

