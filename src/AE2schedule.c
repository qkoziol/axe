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
#include "AE2threadpool.h"


/*
 * Local typedefs
 */
struct AE2_schedule_t {
    OPA_Queue_info_t        scheduled_queue;        /* Queue of tasks that are "scheduled" (can be executed now) */
    pthread_mutex_t         scheduled_queue_mutex;  /* Mutex for dequeueing from scheduled_queue */
    OPA_int_t               sleeping_workers;       /* # of worker threads not guaranteed to try dequeueing tasks before they complete */
    OPA_int_t               num_tasks;              /* # of tasks in scheduler */
    OPA_int_t               all_tasks_done;         /* Whether all tasks are done (i.e. num_tasks == 0).  Separate variable so we only have to lock wait_all_mutex when num_tasks drops to 0 */
    pthread_cond_t          wait_all_cond;          /* Condition variable for waiting for all tasks to complete */
    pthread_mutex_t         wait_all_mutex;         /* Mutex for waiting for all tasks to complete */
    AE2_task_int_t          task_list_head;         /* Sentinel task for head of task list (only used for destroying all tasks) */
    AE2_task_int_t          task_list_tail;         /* Sentinel task for tail of task list */
    pthread_mutex_t         task_list_mutex;        /* Mutex for task list */
};


AE2_error_t
AE2_schedule_create(size_t num_threads, AE2_schedule_t **schedule/*out*/)
{
    _Bool is_queue_mutex_init = FALSE;
    _Bool is_wait_all_cond_init = FALSE;
    _Bool is_wait_all_mutex_init = FALSE;
    _Bool is_task_list_mutex_init = FALSE;
    AE2_error_t ret_value = AE2_SUCCEED;

    *schedule = NULL;

    /* Allocate schedule */
    if(NULL == (*schedule = (AE2_schedule_t *)malloc(sizeof(AE2_schedule_t))))
        ERROR;

    /* Initialize scheduled task queue */
    OPA_Queue_init(&(*schedule)->scheduled_queue);

    /* Initialize queue mutex */
    if(0 != pthread_mutex_init(&(*schedule)->scheduled_queue_mutex, NULL))
        ERROR;
    is_queue_mutex_init = TRUE;

    /* Initialize sleeping_threads */
    OPA_store_int(&(*schedule)->sleeping_workers, (int)num_threads);

    /* Initialize number of tasks and all_tasks_done */
    OPA_store_int(&(*schedule)->num_tasks, 0);
    OPA_store_int(&(*schedule)->all_tasks_done, TRUE);

    /* Initialize wait_all condition variable */
    if(0 != pthread_cond_init(&(*schedule)->wait_all_cond, NULL))
        ERROR;
    is_wait_all_cond_init = TRUE;

    /* Initialize wait_all mutex */
    if(0 != pthread_mutex_init(&(*schedule)->wait_all_mutex, NULL))
        ERROR;
    is_wait_all_mutex_init = TRUE;

    /* Initialize task list sentinels (other fields of sentinels are not used
     * and can be left uninitialized) */
    (*schedule)->task_list_head.task_list_next = &(*schedule)->task_list_tail;
    (*schedule)->task_list_head.task_list_prev = NULL;
    (*schedule)->task_list_tail.task_list_next = NULL;
    (*schedule)->task_list_tail.task_list_prev = &(*schedule)->task_list_head;

    /* Initialize task list mutex */
    if(0 != pthread_mutex_init(&(*schedule)->task_list_mutex, NULL))
        ERROR;
    is_task_list_mutex_init = TRUE;

done:
    if(ret_value == AE2_FAIL)
        if(schedule) {
            if(is_queue_mutex_init)
                (void)pthread_mutex_destroy(&(*schedule)->scheduled_queue_mutex);
            if(is_wait_all_cond_init)
                (void)pthread_cond_destroy(&(*schedule)->wait_all_cond);
            if(is_wait_all_mutex_init)
                (void)pthread_mutex_destroy(&(*schedule)->wait_all_mutex);
            if(is_task_list_mutex_init)
                (void)pthread_mutex_destroy(&(*schedule)->task_list_mutex);
            free(*schedule);
            *schedule = NULL;
        } /* end if */

    return ret_value;
} /* end AE2_schedule_create() */


void
AE2_schedule_worker_running(AE2_schedule_t *schedule)
{
    OPA_decr_int(&schedule->sleeping_workers);
    assert(OPA_load_int(&schedule->sleeping_workers) >= 0);

    return;
} /* end AE2_schedule_worker_running() */


AE2_error_t
AE2_schedule_add(AE2_task_int_t *task)
{
    AE2_schedule_t *schedule;
    AE2_task_int_t *parent_task;
    size_t i;
    AE2_error_t ret_value = AE2_SUCCEED;

    assert(task);
    assert(task->engine);
    assert((AE2_status_t)OPA_load_int(&task->status) == AE2_WAITING_FOR_PARENT);
    assert(!task->sufficient_parents == (_Bool)OPA_load_int(&task->sufficient_complete));

    schedule = task->engine->schedule;

    /* Increment the number of tasks, and set all_tasks_done to FALSE if this
     * was the first task */
    if(OPA_fetch_and_incr_int(&schedule->num_tasks) == 0)
        OPA_store_int(&schedule->all_tasks_done, FALSE);

    /* Increment the reference count on the task due to it being placed in the
     * scheduler */
#ifdef NAF_DEBUG_REF
    printf("AE2_schedule_add: incr ref: %p", task);
#endif /* NAF_DEBUG_REF */
    AE2_task_incr_ref(task);

    /* Note that a write barrier is only not necessary here because other
     * threads can only reach this task if this thread takes a mutex, implying a
     * barrier.  If we ever remove the mutex we will need a write barrier before
     * this task is added to a child array. */

    /* Loop over necessary parents, adding this task as a child to each */
    for(i = 0; i < task->num_necessary_parents; i++) {
        parent_task = task->necessary_parents[i];

        /* Increment reference count on parent task */
#ifdef NAF_DEBUG_REF
        printf("AE2_schedule_add: incr ref: %p nec_par", parent_task);
#endif /* NAF_DEBUG_REF */
        AE2_task_incr_ref(parent_task);

        /* Lock parent task mutex.  Note that this thread does not hold any
         * other locks and it does not take any others before releasing this
         * one. */
        if(0 != pthread_mutex_lock(&parent_task->task_mutex))
            ERROR;

        /* Check if the parent is complete */
        if((AE2_status_t)OPA_load_int(&parent_task->status) == AE2_TASK_DONE)
            /* Parent is complete, increment number of necessary tasks complete
             */
            /* Will need to add a check for cancelled state here when remove,
             * etc. implemented */
            OPA_incr_int(&task->num_conditions_complete);
        else {
            /* Increment reference count on child task, so child does not get freed
             * before necessary parent finishes.  This could only happen if the
             * child gets cancelled/removed. */
#ifdef NAF_DEBUG_REF
            printf("AE2_schedule_add: incr ref: %p from nec_par", task);
#endif /* NAF_DEBUG_REF */
            AE2_task_incr_ref(task);

            /* Add this task to parent's child task list */
            if(parent_task->num_necessary_children
                    == parent_task->necessary_children_nalloc) {
                /* Grow/alloc array */
                if(parent_task->necessary_children_nalloc) {
                    assert(parent_task->necessary_children);
                    if(NULL == (parent_task->necessary_children = (AE2_task_int_t **)realloc(parent_task->necessary_children, 2 * parent_task->necessary_children_nalloc * sizeof(AE2_task_int_t *)))) {
                        (void)pthread_mutex_unlock(&parent_task->task_mutex);
                        ERROR;
                    } /* end if */
                    parent_task->necessary_children_nalloc *= 2;
                } /* end if */
                else {
                    assert(!parent_task->necessary_children);
                    if(NULL == (parent_task->necessary_children = (AE2_task_int_t **)malloc(AE2_TASK_NCHILDREN_INIT * sizeof(AE2_task_int_t *)))) {
                        (void)pthread_mutex_unlock(&parent_task->task_mutex);
                        ERROR;
                    } /* end if */
                    parent_task->necessary_children_nalloc = AE2_TASK_NCHILDREN_INIT;
                } /* end else */
            } /* end else */
            assert(parent_task->necessary_children_nalloc > parent_task->num_necessary_children);

            /* Add to list */
            parent_task->necessary_children[parent_task->num_necessary_children] = task;
            parent_task->num_necessary_children++;
        } /* end else */

        /* Release lock on parent task */
        if(0 != pthread_mutex_unlock(&parent_task->task_mutex))
            ERROR;
    } /* end for */

    /* Loop over sufficient parents, adding this task as a child to each */
    for(i = 0; i < task->num_sufficient_parents; i++) {
        parent_task = task->sufficient_parents[i];

        /* Increment reference count on parent task */
#ifdef NAF_DEBUG_REF
        printf("AE2_schedule_add: incr ref: %p suf_par", parent_task);
#endif /* NAF_DEBUG_REF */
        AE2_task_incr_ref(parent_task);

        /* Lock parent task mutex.  Note that this thread does not hold any
         * other locks and it does not take any others before releasing this
         * one. */
        if(0 != pthread_mutex_lock(&parent_task->task_mutex))
            ERROR;

        /* Check if the parent is complete */
        if((AE2_status_t)OPA_load_int(&parent_task->status) == AE2_TASK_DONE) {
            /* Parent is complete, mark sufficient condition as fulfilled if it
             * was not previously and adjust num_necessary_complete */
            if(!OPA_load_int(&task->sufficient_complete)) {
                OPA_store_int(&task->sufficient_complete, TRUE);
                OPA_incr_int(&task->num_conditions_complete);
            } /* end if */
        } /* end if */
        else {
            /* Increment reference count on child task, so child does not get freed
             * before sufficient parent finishes */
#ifdef NAF_DEBUG_REF
            printf("AE2_schedule_add: incr ref: %p from suf_par", task);
#endif /* NAF_DEBUG_REF */
            AE2_task_incr_ref(task);

            /* Add this task to parent's child task list */
            if(parent_task->num_sufficient_children
                    == parent_task->sufficient_children_nalloc) {
                /* Grow/alloc array */
                if(parent_task->sufficient_children_nalloc) {
                    assert(parent_task->sufficient_children);
                    if(NULL == (parent_task->sufficient_children = (AE2_task_int_t **)realloc(parent_task->sufficient_children, 2 * parent_task->sufficient_children_nalloc * sizeof(AE2_task_int_t *)))) {
                        (void)pthread_mutex_unlock(&parent_task->task_mutex);
                        ERROR;
                    } /* end if */
                    parent_task->sufficient_children_nalloc *= 2;
                } /* end if */
                else {
                    assert(!parent_task->sufficient_children);
                    if(NULL == (parent_task->sufficient_children = (AE2_task_int_t **)malloc(AE2_TASK_NCHILDREN_INIT * sizeof(AE2_task_int_t *)))) {
                        (void)pthread_mutex_unlock(&parent_task->task_mutex);
                        ERROR;
                    } /* end if */
                    parent_task->sufficient_children_nalloc = AE2_TASK_NCHILDREN_INIT;
                } /* end else */
            } /* end else */
            assert(parent_task->sufficient_children_nalloc > parent_task->num_sufficient_children);

            /* Add to list */
            parent_task->sufficient_children[parent_task->num_sufficient_children] = task;
            parent_task->num_sufficient_children++;
        } /* end else */

        /* Release lock on parent task */
        if(0 != pthread_mutex_unlock(&parent_task->task_mutex))
            ERROR;
    } /* end for */

    assert((size_t)OPA_load_int(&task->num_conditions_complete) <= task->num_necessary_parents + 1);

#ifdef NAF_DEBUG
    printf("AE2_schedule_add: added %p\n", task); fflush(stdout);
#endif /* NAF_DEBUG */

    /* Add task to task list */
    /* Lock task list mutex */
    if(0 != pthread_mutex_lock(&schedule->task_list_mutex))
        ERROR;

    /* Update list */
    assert(schedule->task_list_head.task_list_next);
    assert(schedule->task_list_head.task_list_next->task_list_prev == &schedule->task_list_head);
    task->task_list_next = schedule->task_list_head.task_list_next;
    task->task_list_prev = &schedule->task_list_head;
    schedule->task_list_head.task_list_next = task;
    task->task_list_next->task_list_prev = task;

    /* Unlock task list mutex */
    if(0 != pthread_mutex_unlock(&schedule->task_list_mutex))
        ERROR;

    /* Increment num_conditions_complete to account for initialization being
     * complete. Schedule the event if all necessary parents and at least one
     * sufficient parent are complete. */
    if((size_t)OPA_fetch_and_incr_int(&task->num_conditions_complete)
                == task->num_necessary_parents + 1) {
        AE2_thread_t *thread = NULL;
        AE2_task_int_t *exec_task = NULL;

        /* The fetch-and-incr should guarantee (along with similar constructions
         * in AE2_schedule_finish) that only one thread ever sees the last
         * condition fulfilled, so no need to do compare-and-swap on the status
         */
        assert((AE2_status_t)OPA_load_int(&task->status) == AE2_WAITING_FOR_PARENT);

        /* Update status */
        OPA_store_int(&task->status, AE2_TASK_SCHEDULED);

        /* Write barrier so we know that all changes to the task struct are
         * visible before we enqueue this task and subject it to being picked up
         * by a worker thread.  This is not necessary if this task is not
         * scheduled because the only ways this task could be reached again
         * involve taking mutexes (either through this function or
         * AE2_schedule_finish()). */
        OPA_write_barrier();

#ifdef NAF_DEBUG
        printf("AE2_schedule_add: enqueue %p\n", task); fflush(stdout);
#endif /* NAF_DEBUG */

        /* Add task to scheduled queue */
        OPA_Queue_enqueue(&schedule->scheduled_queue, task, AE2_task_int_t, scheduled_queue_hdr);

        /*
         * Now try to execute the event
         */
        /* Note that we only try to take one task from the queue in this
         * function because we only pushed one onto the queue.  If there is more
         * than one task in the queue, then either the thread pool is full or
         * there is another thread scheduling tasks which will execute at least
         * as many as it pushes.  In either case there is no need to pull more
         * than one thread. */
        /* To prevent the race condition where a worker threads are past past
         * the point where they look for tasks but have not yet been released to
         * the thread pool, delaying or preventing execution of this task, loop
         * until we either get a thread or get confirmation that all threads are
         * busy and will attempt to acquire a task when they complete */
        do {
            /* Try to retrieve a thread from the thread pool */
            if(AE2_thread_pool_try_acquire(task->engine->thread_pool, &thread) != AE2_SUCCEED)
                ERROR;

            /* If we have a thread we can exit */
            if(thread)
                break;

            /* Read barrier so the check on sleeping_threads happens after
             * the failed acquire */
            OPA_read_barrier();

            /* Check if all workers are busy and guaranteed to check the
             * schedule before sleeping */
            if(OPA_load_int(&schedule->sleeping_workers) == 0)
                /* All workers are busy and the first one to finish will pick up
                 * this task.  We can go ahead and return */
                break;
            else
                /* The queue was empty but at least one worker was possibly soon
                 * to be finished.  Give the finishing workers a chance to
                 * finish */
                AE2_YIELD();
        } while(1);

        /* Check if we were able to acquire a thread */
        if(thread) {
            /* Now retrieve a task from the scheduled task queue */
            /* Lock scheduled queue mutex.  This mutex must be released
             * immediately after the dequeue. */
            if(0 != pthread_mutex_lock(&schedule->scheduled_queue_mutex))
                ERROR;

            /* Check if queue is empty */
            if(OPA_Queue_is_empty(&schedule->scheduled_queue)) {
                /* Unlock scheduled queue mutex */
                if(0 != pthread_mutex_unlock(&schedule->scheduled_queue_mutex))
                    ERROR;

                /* Release thread back to thread pool */
                AE2_thread_pool_release(thread);
                thread = NULL;
            } /* end if */
            else {
                /* Retrieve task from scheduled queue */
                OPA_Queue_dequeue(&schedule->scheduled_queue, exec_task, AE2_task_int_t, scheduled_queue_hdr);

#ifdef NAF_DEBUG
printf("AE2_schedule_add: dequeue %p\n", exec_task); fflush(stdout);
#endif /* NAF_DEBUG */

                /* Unlock scheduled queue mutex */
                if(0 != pthread_mutex_unlock(&schedule->scheduled_queue_mutex))
                    ERROR;

                assert(exec_task);

                /* Launch the task */
                if(AE2_thread_pool_launch(thread, AE2_task_worker, exec_task) != AE2_SUCCEED)
                    ERROR;
            } /* end else */
        } /* end if */
    } /* end if */

done:
    return ret_value;
} /* end AE2_schedule_add() */


AE2_error_t
AE2_schedule_finish(AE2_task_int_t **task/*in,out*/)
{
    AE2_task_int_t *child_task;
    AE2_schedule_t *schedule;
    AE2_thread_t *thread;
    size_t i;
    AE2_error_t ret_value = AE2_SUCCEED;

    assert(task);
    assert(*task);

    schedule = (*task)->engine->schedule;

    /* Acquire task mutex while iterating over child arrays and changing state.
     * No other mutexes will be acquired while we hold this one. */
    if(0 != pthread_mutex_lock(&(*task)->task_mutex))
        ERROR;

    /* Update all necessary children */
    for(i = 0; i < (*task)->num_necessary_children; i++) {
        child_task = (*task)->necessary_children[i];

        /* Check if this was the last condition fulfilled for the child (i.e.
         * this is the last necessary parent, the sufficient condition is
         * fulfilled, and the task is initialized) */
        if(OPA_fetch_and_incr_int(&child_task->num_conditions_complete)
                == child_task->num_necessary_parents + 1) {
            /* The task can be scheduled - enqueue it */
            /* The fetch-and-incr should guarantee (along with similar
             * constructions elsewhere in this function and in AE2_schedule_add)
             * that only one thread ever sees the last condition fulfilled, but
             * we still need compare-and-swap in case this task has been
             * canceled */
            if((AE2_status_t)OPA_cas_int(&child_task->status,
                    (int)AE2_WAITING_FOR_PARENT, (int)AE2_TASK_SCHEDULED)
                    == AE2_WAITING_FOR_PARENT) {
                /* Write barrier to make sure the status is updated before
                 * the task is scheduled */
                OPA_write_barrier();

#ifdef NAF_DEBUG
                printf("AE2_schedule_finish: enqueue %p nec\n", child_task); fflush(stdout);
#endif /* NAF_DEBUG */

                /* Add task to scheduled queue */
                OPA_Queue_enqueue(&schedule->scheduled_queue, child_task, AE2_task_int_t, scheduled_queue_hdr);
            } /* end if */
            else
                assert((AE2_status_t)OPA_load_int(&child_task->status) == AE2_TASK_CANCELED);
        } /* end if */

        /* Decrement ref count on child */
#ifdef NAF_DEBUG_REF
        printf("AE2_schedule_finish: decr ref: %p nec", child_task);
#endif /* NAF_DEBUG_REF */
        AE2_task_decr_ref(child_task);
    } /* end for */

    /* Update all sufficient children */
    for(i = 0; i < (*task)->num_sufficient_children; i++) {
        child_task = (*task)->sufficient_children[i];

        /* Mark the sufficient condition as complete and check if this was the
         * first sufficient parent to complete for the child */
        if(OPA_swap_int(&child_task->sufficient_complete, TRUE) == FALSE)
            /* Increment num_conditions_complete and check if this was the last
             * condition needed  (i.e. all necessary parents were complete and
             * the initialization is complete) */
            if(OPA_fetch_and_incr_int(&child_task->num_conditions_complete)
                == child_task->num_necessary_parents + 1) {
                /* The task can be scheduled - enqueue it */
                /* The fetch-and-incr should guarantee (along with similar
                 * constructions elsewhere in this function and in
                 * AE2_schedule_add) that only one thread ever sees the last
                 * condition fulfilled, but we still need compare-and-swap in
                 * case this task has been canceled */
                if((AE2_status_t)OPA_cas_int(&child_task->status,
                        (int)AE2_WAITING_FOR_PARENT, (int)AE2_TASK_SCHEDULED)
                        == AE2_WAITING_FOR_PARENT) {

                    /* Write barrier to make sure the status is updated before
                     * the task is scheduled */
                    OPA_write_barrier();

#ifdef NAF_DEBUG
                    printf("AE2_schedule_finish: enqueue %p suf\n", child_task); fflush(stdout);
#endif /* NAF_DEBUG */

                    /* Add task to scheduled queue */
                    OPA_Queue_enqueue(&schedule->scheduled_queue, child_task, AE2_task_int_t, scheduled_queue_hdr);
                } /* end if */
                else
                    assert((AE2_status_t)OPA_load_int(&child_task->status) == AE2_TASK_CANCELED);
            } /* end if */

        /* Decrement ref count on child */
#ifdef NAF_DEBUG_REF
        printf("AE2_schedule_finish: decr ref: %p suf", child_task);
#endif /* NAF_DEBUG_REF */
        AE2_task_decr_ref(child_task);
    } /* end for */

    /* Lock wait mutex before we change stat to done, so AE2_task_wait knows
     * that if the task is not marked done it is safe to wait on the condition.
     */
    /* It should be possible to eliminate this lock and broadcast unless a
     * thread actually needs it by using a field in the thread struct to keep
     * track of whether any threads are waiting on this task and careful
     * ordering of operations.  This function would first set the status to
     * DONE, do a read/barrier, then check the waiting field. AE2_task_wait()
     * would first set the waiting field, do a read/write barrier, then check
     * the task status.  I am not sure how much faster this would be (if any)
     * than the simpler/more obvious implementation below.  A similar note
     * applies to the wait_all implementation as well.  -NAF */
    if(0 != pthread_mutex_lock(&(*task)->wait_mutex))
        ERROR;

#ifdef NAF_DEBUG
    printf("AE2_schedule_finish: %p->status = AE2_TASK_DONE\n", *task); fflush(stdout);
#endif /* NAF_DEBUG */

    OPA_store_int(&(*task)->status, (int)AE2_TASK_DONE);

    /* Release task mutex */
    if(0 != pthread_mutex_unlock(&(*task)->task_mutex))
        ERROR;

    /* Signal threads waiting on this task to complete */
    if(0 != pthread_cond_broadcast(&(*task)->wait_cond))
        ERROR;

    /* Unlock wait mutex */
    if(0 != pthread_mutex_unlock(&(*task)->wait_mutex))
        ERROR;

    /* Decrement the number of tasks and if this was the last task signal
     * threads waiting for all tasks to complete */
    if(OPA_decr_and_test_int(&schedule->num_tasks)) {
        /* Lock wait_all mutex, before changing all_tasks_done to avoid race
         * condition */
        if(0 != pthread_mutex_lock(&schedule->wait_all_mutex))
            ERROR;

        /* Mark all tasks as done */
        OPA_store_int(&schedule->all_tasks_done, TRUE);

        /* Signal threads waiting on all tasks to complete */
        if(0 != pthread_cond_broadcast(&schedule->wait_all_cond))
            ERROR;

        /* Unlock wait_all mutex */
        if(0 != pthread_mutex_unlock(&schedule->wait_all_mutex))
            ERROR;
    } /* end if */

    /* Note: if we ever switch to a lockfree algorithm, we will need to add a
     * read/write barrier here to ensure consistency across client operator
     * tasks and to ensure that the status is updated before decrementing the
     * ref count */

    /* Decrement ref count - this task is complete and no longer part of the
     * schedule */
#ifdef NAF_DEBUG_REF
    printf("AE2_schedule_finish: decr ref: %p", *task);
#endif /* NAF_DEBUG_REF */
    AE2_task_decr_ref(*task);

    /* Now try to launch all scheduled tasks, until we run out of tasks or run
     * out of threads.  If we run out of threads first, we will return the last
     * task dequeued to the caller, which will run it in this thread. */
    do {
        /* Mark this thread as sleeping, because this might be the last time we
         * check the scheduled queue */
        /* Note no read/write barrier is needed after this only because of the
         * mutex */
        OPA_incr_int(&schedule->sleeping_workers);

        /* Lock scheduled queue mutex.  This mutex must be released
         * immediately after the dequeue. */
        if(0 != pthread_mutex_lock(&schedule->scheduled_queue_mutex))
            ERROR;

        /* Check if queue is empty */
        if(OPA_Queue_is_empty(&schedule->scheduled_queue)) {
            /* Unlock scheduled queue mutex */
            if(0 != pthread_mutex_unlock(&schedule->scheduled_queue_mutex))
                ERROR;

            /* We did not find a task or a thread */
            *task = NULL;
            thread = NULL;
        } /* end if */
        else {
            /* We got a task so we are not sleeping any more */
            OPA_decr_int(&schedule->sleeping_workers);

            /* Retrieve task from scheduled queue */
            OPA_Queue_dequeue(&schedule->scheduled_queue, *task, AE2_task_int_t, scheduled_queue_hdr);

#ifdef NAF_DEBUG
            printf("AE2_schedule_finish: dequeue %p\n", *task); fflush(stdout);
#endif /* NAF_DEBUG */

            /* Unlock scheduled queue mutex */
            if(0 != pthread_mutex_unlock(&schedule->scheduled_queue_mutex))
                ERROR;

            assert(*task);

            /* Try to retrieve a thread from the thread pool */
            if(AE2_thread_pool_try_acquire((*task)->engine->thread_pool, &thread) != AE2_SUCCEED)
                ERROR;

            if(thread)
                /* Launch the task */
                if(AE2_thread_pool_launch(thread, AE2_task_worker, *task) != AE2_SUCCEED)
                    ERROR;
        } /* end else */
    } while(task && thread);

done:
    return ret_value;
} /* end AE2_schedule_finish() */


AE2_error_t
AE2_schedule_wait_all(AE2_schedule_t *schedule)
{
    AE2_error_t ret_value = AE2_SUCCEED;

    assert(schedule);

    /* Lock wait_all mutex.  Do so before checking the status so we know that
     * (together with the similar mutex in AE2_schedule_finish()) if
     * all_tasks_done is not TRUE that we will be woken up from
     * pthread_cond_wait() when the task is complete, i.e. the signal will not
     * be sent before this thread begins waiting. */
    if(0 != pthread_mutex_lock(&schedule->wait_all_mutex))
        ERROR;

    /* Check if all tasks are already complete */
    if(!OPA_load_int(&schedule->all_tasks_done))
        /* Wait for signal */
        if(0 != pthread_cond_wait(&schedule->wait_all_cond, &schedule->wait_all_mutex))
            ERROR;

    /* Unlock wait_all mutex */
    if(0 != pthread_mutex_unlock(&schedule->wait_all_mutex))
        ERROR;

done:
    return ret_value;
} /* end AE2_schedule_wait_all() */


void
AE2_schedule_cancel_all(AE2_schedule_t *schedule)
{
    AE2_task_int_t *task;

    assert(schedule);

    /* Loop over all tasks in the task list, marking all that are not running or
     * done as canceled */
    for(task = schedule->task_list_head.task_list_next;
            task != &schedule->task_list_tail;
            task = task->task_list_next)
        if((AE2_status_t)OPA_cas_int(&task->status, (int)AE2_WAITING_FOR_PARENT,
                (int)AE2_TASK_CANCELED) != AE2_WAITING_FOR_PARENT)
            (void)OPA_cas_int(&task->status, (int)AE2_TASK_SCHEDULED,
                    (int)AE2_TASK_CANCELED);

    return;
} /* end AE2_cancel_all() */


AE2_error_t
AE2_schedule_remove_task(AE2_task_int_t *task)
{
    AE2_error_t ret_value = AE2_SUCCEED;

    /* Lock task list mutex */
    if(0 != pthread_mutex_lock(&task->engine->schedule->task_list_mutex))
        ERROR;

    /* Update list */
    assert(task->task_list_next);
    assert(task->task_list_prev);
    assert(task->task_list_next->task_list_prev == task);
    assert(task->task_list_prev->task_list_next == task);
    task->task_list_next->task_list_prev = task->task_list_prev;
    task->task_list_prev->task_list_next = task->task_list_next;

    /* Unlock task list mutex */
    if(0 != pthread_mutex_unlock(&task->engine->schedule->task_list_mutex))
        ERROR;

done:
    return ret_value;
} /* end AE2_schedule_remove_task() */


AE2_error_t
AE2_schedule_free(AE2_schedule_t *schedule)
{
    AE2_task_int_t *task;
    AE2_task_int_t *next;
    AE2_error_t ret_value = AE2_SUCCEED;

    assert(schedule);

    /* Free all remaining tasks.  They should all be done or canceled. */
    for(task = schedule->task_list_head.task_list_next;
            task != &schedule->task_list_tail;
            task = next) {
        assert(((AE2_status_t)OPA_load_int(&task->status) == AE2_TASK_CANCELED) || ((AE2_status_t)OPA_load_int(&task->status) == AE2_TASK_DONE));

        /* Cache next task because task will be freed */
        next = task->task_list_next;

        /* Set task_list_next pointer to NULL so AE2_task_free() doesn't bother
         * calling AE2_schedule_remove_task() */
        task->task_list_next = NULL;

        /* Free task */
        AE2_task_free(task);
    } /* end for */

    /* Destroy queue mutex */
    if(0 != pthread_mutex_destroy(&schedule->scheduled_queue_mutex))
        ERROR;

    /* Destroy wait_all condition variable */
    if(0 != pthread_cond_destroy(&schedule->wait_all_cond))
        ERROR;

    /* Destroy wait_all mutex */
    if(0 != pthread_mutex_destroy(&schedule->wait_all_mutex))
        ERROR;

    /* Destroy task list mutex */
    if(0 != pthread_mutex_destroy(&schedule->task_list_mutex))
        ERROR;

    /* Free schedule */
    free(schedule);

done:
    return ret_value;
} /* end AE2_schedule_free() */

