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
#include "AXEthreadpool.h"


/*
 * Local typedefs
 */
/* Schedule structure */
struct AXE_schedule_t {
    OPA_Queue_info_t        scheduled_queue;        /* Queue of tasks that are "scheduled" (can be executed now) */
    pthread_mutex_t         scheduled_queue_mutex;  /* Mutex for dequeueing from scheduled_queue */
    OPA_int_t               sleeping_workers;       /* # of worker threads not guaranteed to try dequeueing tasks before they complete */
    OPA_int_t               closing;                /* Whether we are shutting down the scheduler.  Currently only used to prevent a rare race condition.  Could be expanded to short-circuit traversal of canceled tasks when shutting down. */
    OPA_int_t               num_tasks;              /* # of tasks in scheduler */
    pthread_cond_t          wait_all_cond;          /* Condition variable for waiting for all tasks to complete */
    pthread_mutex_t         wait_all_mutex;         /* Mutex for waiting for all tasks to complete */
    AXE_task_int_t          task_list_head;         /* Sentinel task for head of task list */
    AXE_task_int_t          task_list_tail;         /* Sentinel task for tail of task list */
    pthread_mutex_t         task_list_mutex;        /* Mutex for task list.  Must not be taken while holding a task mutex! */
#ifdef AXE_DEBUG_NTASKS
    OPA_int_t               nadds;
    OPA_int_t               nenqueues;
    OPA_int_t               ndequeues;
    OPA_int_t               ncomplete;
    OPA_int_t               ncancels;
#endif /* AXE_DEBUG_NTASKS */
};


/*
 * Local functions
 */
static AXE_error_t AXE_schedule_add_common(AXE_task_int_t *task);


/*
 * Debugging
 */
#ifdef AXE_DEBUG_PERF
OPA_int_t AXE_debug_nspins_add = OPA_INT_T_INITIALIZER(0);
OPA_int_t AXE_debug_nspins_finish = OPA_INT_T_INITIALIZER(0);
OPA_int_t AXE_debug_nadds = OPA_INT_T_INITIALIZER(0);
#endif /* AXE_DEBUG_PERF */


/*-------------------------------------------------------------------------
 * Function:    AXE_schedule_create
 *
 * Purpose:     Creates a schedule for a thread pool with the specified
 *              number of threads.  The scheduler needs to know the numer
 *              of threads in order to guarantee that at least that many
 *              threads will be available for simultaneous executiong, in
 *              case the application algorithm depends on it.
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
AXE_schedule_create(size_t num_threads, AXE_schedule_t **schedule/*out*/)
{
    _Bool is_queue_mutex_init = FALSE;
    _Bool is_wait_all_cond_init = FALSE;
    _Bool is_wait_all_mutex_init = FALSE;
    _Bool is_task_list_mutex_init = FALSE;
    AXE_error_t ret_value = AXE_SUCCEED;

    *schedule = NULL;

    /* Allocate schedule */
    if(NULL == (*schedule = (AXE_schedule_t *)malloc(sizeof(AXE_schedule_t))))
        ERROR;

    /* Initialize scheduled task queue */
    OPA_Queue_init(&(*schedule)->scheduled_queue);

    /* Initialize queue mutex */
    if(0 != pthread_mutex_init(&(*schedule)->scheduled_queue_mutex, NULL))
        ERROR;
    is_queue_mutex_init = TRUE;

    /* Initialize sleeping_threads and closing */
    OPA_store_int(&(*schedule)->sleeping_workers, (int)num_threads);
    OPA_store_int(&(*schedule)->closing, FALSE);

    /* Initialize number of tasks */
    OPA_store_int(&(*schedule)->num_tasks, 0);

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

#ifdef AXE_DEBUG_NTASKS
    OPA_store_int(&(*schedule)->nadds, 0);
    OPA_store_int(&(*schedule)->nenqueues, 0);
    OPA_store_int(&(*schedule)->ndequeues, 0);
    OPA_store_int(&(*schedule)->ncomplete, 0);
    OPA_store_int(&(*schedule)->ncancels, 0);
#endif /* AXE_DEBUG_NTASKS */

done:
    if(ret_value == AXE_FAIL)
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
} /* end AXE_schedule_create() */


/*-------------------------------------------------------------------------
 * Function:    AXE_schedule_worker_running
 *
 * Purpose:     Informs the scheduler that a worker is running, and
 *              gauaranteed to check the schedule for new tasks before
 *              returning to the thread pool.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
void
AXE_schedule_worker_running(AXE_schedule_t *schedule)
{
#ifdef NDEBUG
    OPA_decr_int(&schedule->sleeping_workers);
#else /* NDEBUG */
    assert(OPA_fetch_and_decr_int(&schedule->sleeping_workers) > 0);
#endif /* NDEBUG */

    return;
} /* end AXE_schedule_worker_running() */


/*-------------------------------------------------------------------------
 * Function:    AXE_schedule_add
 *
 * Purpose:     Adds the specified task to the schedule.  Updates parents'
 *              "children" arrays and calls AXE_schedule_add_common().
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
AXE_schedule_add(AXE_task_int_t *task)
{
    AXE_task_int_t *parent_task;
    size_t i;
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(task);
    assert(task->engine);
    assert((AXE_status_t)OPA_load_int(&task->status) == AXE_WAITING_FOR_PARENT);
    assert(!task->sufficient_parents == (_Bool)OPA_load_int(&task->sufficient_complete));

    /* Increment the reference count on the task due to it being placed in the
     * scheduler */
#ifdef AXE_DEBUG_REF
    printf("AXE_schedule_add: incr ref: %p\n", task); fflush(stdout);
#endif /* AXE_DEBUG_REF */
    AXE_task_incr_ref(task);

    /* Note that a write barrier is only not necessary here because other
     * threads can only reach this task if this thread takes a mutex, implying a
     * barrier.  If we ever remove the mutex we will need a write barrier before
     * this task is added to a child array. */

    /* Loop over necessary parents, adding this task as a child to each */
    for(i = 0; i < task->num_necessary_parents; i++) {
        parent_task = task->necessary_parents[i];

        /* Increment reference count on parent task */
#ifdef AXE_DEBUG_REF
        printf("AXE_schedule_add: incr ref: %p nec_par\n", parent_task); fflush(stdout);
#endif /* AXE_DEBUG_REF */
        AXE_task_incr_ref(parent_task);

        /* Lock parent task mutex.  Note that this thread does not hold any
         * other locks and it does not take any others before releasing this
         * one. */
#ifdef AXE_DEBUG_LOCK
        printf("AXE_schedule_add: lock task_mutex: %p nec_par\n", &parent_task->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
        if(0 != pthread_mutex_lock(&parent_task->task_mutex))
            ERROR;

        /* Check if the parent is complete */
        if((AXE_status_t)OPA_load_int(&parent_task->status) == AXE_TASK_DONE)
            /* Parent is complete, increment number of necessary tasks complete
             */
            /* Will need to add a check for cancelled state here when remove,
             * etc. implemented */
            OPA_incr_int(&task->num_conditions_complete);
        else if((AXE_status_t)OPA_load_int(&parent_task->status) == AXE_TASK_CANCELED) {
            /* Parent is canceled.  If this happens, return an error but keep
             * this task present and mark it canceled so things get cleaned up
             * properly. */
            OPA_store_int(&task->status, (int)AXE_TASK_CANCELED);

            /* Increment the number of conditions complete to account for this
             * parent not having this task in its child list.  Since it is
             * canceled, it does not matter if this task is processed earlier
             * than the application might expect. */
            OPA_incr_int(&task->num_conditions_complete);

            ret_value = AXE_FAIL;
        } /* end if */
        else {
            /* Add this task to parent's child task list */
            if(parent_task->num_necessary_children
                    == parent_task->necessary_children_nalloc) {
                /* Grow/alloc array */
                if(parent_task->necessary_children_nalloc) {
                    assert(parent_task->necessary_children);
                    if(NULL == (parent_task->necessary_children = (AXE_task_int_t **)realloc(parent_task->necessary_children, 2 * parent_task->necessary_children_nalloc * sizeof(AXE_task_int_t *)))) {
                        (void)pthread_mutex_unlock(&parent_task->task_mutex);
                        ERROR;
                    } /* end if */
                    parent_task->necessary_children_nalloc *= 2;
                } /* end if */
                else {
                    assert(!parent_task->necessary_children);
                    if(NULL == (parent_task->necessary_children = (AXE_task_int_t **)malloc(AXE_TASK_NCHILDREN_INIT * sizeof(AXE_task_int_t *)))) {
                        (void)pthread_mutex_unlock(&parent_task->task_mutex);
                        ERROR;
                    } /* end if */
                    parent_task->necessary_children_nalloc = AXE_TASK_NCHILDREN_INIT;
                } /* end else */
            } /* end if */
            assert(parent_task->necessary_children_nalloc > parent_task->num_necessary_children);

            /* Increment reference count on child task, so child does not get freed
             * before necessary parent finishes.  This could only happen if the
             * child gets cancelled/removed. */
#ifdef AXE_DEBUG_REF
            printf("AXE_schedule_add: incr ref: %p from nec_par\n", task); fflush(stdout);
#endif /* AXE_DEBUG_REF */
            AXE_task_incr_ref(task);

            /* Add to list */
            parent_task->necessary_children[parent_task->num_necessary_children] = task;
            parent_task->num_necessary_children++;
        } /* end else */

        /* Release lock on parent task */
#ifdef AXE_DEBUG_LOCK
        printf("AXE_schedule_add: unlock task_mutex: %p nec_par\n", &parent_task->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
        if(0 != pthread_mutex_unlock(&parent_task->task_mutex))
            ERROR;
    } /* end for */

    /* Loop over sufficient parents, adding this task as a child to each */
    for(i = 0; i < task->num_sufficient_parents; i++) {
        parent_task = task->sufficient_parents[i];

        /* Increment reference count on parent task */
#ifdef AXE_DEBUG_REF
        printf("AXE_schedule_add: incr ref: %p suf_par\n", parent_task); fflush(stdout);
#endif /* AXE_DEBUG_REF */
        AXE_task_incr_ref(parent_task);

        /* Lock parent task mutex.  Note that this thread does not hold any
         * other locks and it does not take any others before releasing this
         * one. */
#ifdef AXE_DEBUG_LOCK
        printf("AXE_schedule_add: lock task_mutex: %p suf_par\n", &parent_task->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
        if(0 != pthread_mutex_lock(&parent_task->task_mutex))
            ERROR;

        /* Check if the parent is complete */
        if((AXE_status_t)OPA_load_int(&parent_task->status) == AXE_TASK_DONE) {
            /* Parent is complete, mark sufficient condition as fulfilled if it
             * was not previously and adjust num_necessary_complete */
            if(OPA_swap_int(&task->sufficient_complete, TRUE) == FALSE)
                OPA_incr_int(&task->num_conditions_complete);
        } /* end if */
        else if((AXE_status_t)OPA_load_int(&parent_task->status) == AXE_TASK_CANCELED) {
            /* Parent is canceled.  If this happens, return an error but keep
             * this task present and mark it canceled so things get cleaned up
             * properly. */
            OPA_store_int(&task->status, (int)AXE_TASK_CANCELED);

            /* Consider the sufficient condition complete to account for this
             * parent not having this task in its child list.  Since it is
             * canceled, it does not matter if this task is processed earlier
             * than the application might expect. */
            if(OPA_swap_int(&task->sufficient_complete, TRUE) == FALSE)
                OPA_incr_int(&task->num_conditions_complete);

            ret_value = AXE_FAIL;
        } /* end if */
        else {
            /* Add this task to parent's child task list */
            if(parent_task->num_sufficient_children
                    == parent_task->sufficient_children_nalloc) {
                /* Grow/alloc array */
                if(parent_task->sufficient_children_nalloc) {
                    assert(parent_task->sufficient_children);
                    if(NULL == (parent_task->sufficient_children = (AXE_task_int_t **)realloc(parent_task->sufficient_children, 2 * parent_task->sufficient_children_nalloc * sizeof(AXE_task_int_t *)))) {
                        (void)pthread_mutex_unlock(&parent_task->task_mutex);
                        ERROR;
                    } /* end if */
                    parent_task->sufficient_children_nalloc *= 2;
                } /* end if */
                else {
                    assert(!parent_task->sufficient_children);
                    if(NULL == (parent_task->sufficient_children = (AXE_task_int_t **)malloc(AXE_TASK_NCHILDREN_INIT * sizeof(AXE_task_int_t *)))) {
                        (void)pthread_mutex_unlock(&parent_task->task_mutex);
                        ERROR;
                    } /* end if */
                    parent_task->sufficient_children_nalloc = AXE_TASK_NCHILDREN_INIT;
                } /* end else */
            } /* end if */
            assert(parent_task->sufficient_children_nalloc > parent_task->num_sufficient_children);

            /* Increment reference count on child task, so child does not get freed
             * before sufficient parent finishes */
#ifdef AXE_DEBUG_REF
            printf("AXE_schedule_add: incr ref: %p from suf_par\n", task); fflush(stdout);
#endif /* AXE_DEBUG_REF */
            AXE_task_incr_ref(task);

            /* Add to list */
            parent_task->sufficient_children[parent_task->num_sufficient_children] = task;
            parent_task->num_sufficient_children++;
        } /* end else */

        /* Release lock on parent task */
#ifdef AXE_DEBUG_LOCK
        printf("AXE_schedule_add: unlock task_mutex: %p suf_par\n", &parent_task->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
        if(0 != pthread_mutex_unlock(&parent_task->task_mutex))
            ERROR;
    } /* end for */

    assert(((size_t)OPA_load_int(&task->num_conditions_complete) <= task->num_necessary_parents + 1) || ((AXE_status_t)OPA_load_int(&task->status) == AXE_TASK_CANCELED));

#ifdef AXE_DEBUG
    printf("AXE_schedule_add: added %p\n", task); fflush(stdout);
#endif /* AXE_DEBUG */

    /* Finish adding the task to the schedule */
    if(AXE_schedule_add_common(task) != AXE_SUCCEED)
        ERROR;

done:
    return ret_value;
} /* end AXE_schedule_add() */


/*-------------------------------------------------------------------------
 * Function:    AXE_schedule_add_barrier
 *
 * Purpose:     Adds the specified task as a barrier task to the schedule.
 *              Adds all uncomplete and uncanceled tasks in the schedule
 *              without necessary children as necessary parents of task,
 *              then calls AXE_schedule_add_common().
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
AXE_schedule_add_barrier(AXE_task_int_t *task)
{
    AXE_task_int_t *parent_task;
    AXE_status_t parent_status;
    AXE_schedule_t *schedule;
    size_t necessary_parents_nalloc = 0;
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(task);
    assert(task->engine);
    assert((AXE_status_t)OPA_load_int(&task->status) == AXE_WAITING_FOR_PARENT);
    assert(task->num_sufficient_parents == 0);
    assert(!task->sufficient_parents);
    assert(OPA_load_int(&task->sufficient_complete) == TRUE);

    schedule = task->engine->schedule;

    /* Increment the reference count on the task due to it being placed in the
     * scheduler */
#ifdef AXE_DEBUG_REF
    printf("AXE_schedule_add_barrier: incr ref: %p\n", task); fflush(stdout);
#endif /* AXE_DEBUG_REF */
    AXE_task_incr_ref(task);

    /* Note that a write barrier is only not necessary here because other
     * threads can only reach this task if this thread takes a mutex, implying a
     * barrier.  If we ever remove the mutex we will need a write barrier before
     * this task is added to a child array. */

    /* Acquire task list mutex.  We take task mutexes while holding this one.
     * the task list mutex must never (anywhere) be taken while holding a task
     * mutex. */
    if(0 != pthread_mutex_lock(&schedule->task_list_mutex))
        ERROR;

    /* Iterate over all tasks in the scheduler, checking if they should be
     * parents of the barrier task */
    for(parent_task = schedule->task_list_head.task_list_next;
            parent_task != &schedule->task_list_tail;
            parent_task = parent_task->task_list_next) {
        /* Acquire parent task mutex */
#ifdef AXE_DEBUG_LOCK
        printf("AXE_schedule_add_barrier: lock task_mutex: %p\n", &parent_task->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
        if(0 != pthread_mutex_lock(&parent_task->task_mutex))
            ERROR;

        /* Load parent task status */
        parent_status = OPA_load_int(&parent_task->status);

        /* If the task is not canceled or done, and it has no necessary
         * children, add it as a parent of the barrier task.  Okay for checks of
         * these conditions to not be atomic (except load of status), because
         * changing status to done or canceled and modifying or iterating over
         * child arrays occurs while holding the task mutex. */
        if((parent_status != AXE_TASK_DONE)
                && (parent_status != AXE_TASK_CANCELED)
                && (parent_task->num_necessary_children == 0)) {
            /* Add barrier task to parent's child task list */
            if(parent_task->num_necessary_children
                    == parent_task->necessary_children_nalloc) {
                /* Grow/alloc array */
                if(parent_task->necessary_children_nalloc) {
                    assert(parent_task->necessary_children);
                    if(NULL == (parent_task->necessary_children = (AXE_task_int_t **)realloc(parent_task->necessary_children, 2 * parent_task->necessary_children_nalloc * sizeof(AXE_task_int_t *)))) {
                        (void)pthread_mutex_unlock(&parent_task->task_mutex);
                        (void)pthread_mutex_unlock(&schedule->task_list_mutex);
                        ERROR;
                    } /* end if */
                    parent_task->necessary_children_nalloc *= 2;
                } /* end if */
                else {
                    assert(!parent_task->necessary_children);
                    if(NULL == (parent_task->necessary_children = (AXE_task_int_t **)malloc(AXE_TASK_NCHILDREN_INIT * sizeof(AXE_task_int_t *)))) {
                        (void)pthread_mutex_unlock(&parent_task->task_mutex);
                        (void)pthread_mutex_unlock(&schedule->task_list_mutex);
                        ERROR;
                    } /* end if */
                    parent_task->necessary_children_nalloc = AXE_TASK_NCHILDREN_INIT;
                } /* end else */
            } /* end if */
            assert(parent_task->necessary_children_nalloc > parent_task->num_necessary_children);

            /* Increment reference count on barrier task, so it does not get
             * freed before necessary parent finishes.  This could only happen
             * if the child gets cancelled/removed. */
#ifdef AXE_DEBUG_REF
            printf("AXE_schedule_add_barrier: incr ref: %p from nec_par", task); fflush(stdout);
#endif /* AXE_DEBUG_REF */
            AXE_task_incr_ref(task);

            /* Add to list */
            parent_task->necessary_children[parent_task->num_necessary_children] = task;
            parent_task->num_necessary_children++;

            /* Add parent task to barrier's necessary parent task list */
            if(task->num_necessary_parents == necessary_parents_nalloc) {
                /* Grow/alloc array */
                if(necessary_parents_nalloc) {
                    assert(task->necessary_parents);
                    if(NULL == (task->necessary_parents = (AXE_task_int_t **)realloc(task->necessary_parents, 2 * necessary_parents_nalloc * sizeof(AXE_task_int_t *)))) {
                        (void)pthread_mutex_unlock(&parent_task->task_mutex);
                        (void)pthread_mutex_unlock(&schedule->task_list_mutex);
                        ERROR;
                    } /* end if */
                    necessary_parents_nalloc *= 2;
                } /* end if */
                else {
                    assert(!task->necessary_parents);
                    if(NULL == (task->necessary_parents = (AXE_task_int_t **)malloc(AXE_TASK_NCHILDREN_INIT * sizeof(AXE_task_int_t *)))) {
                        (void)pthread_mutex_unlock(&parent_task->task_mutex);
                        (void)pthread_mutex_unlock(&schedule->task_list_mutex);
                        ERROR;
                    } /* end if */
                    necessary_parents_nalloc = AXE_TASK_NCHILDREN_INIT;
                } /* end else */
            } /* end if */
            assert(necessary_parents_nalloc > task->num_necessary_parents);

            /* Increment reference count on parent task */
#ifdef AXE_DEBUG_REF
            printf("AXE_schedule_add_barrier: incr ref: %p nec_par", parent_task); fflush(stdout);
#endif /* AXE_DEBUG_REF */
            AXE_task_incr_ref(parent_task);

            /* Add to list */
            task->necessary_parents[task->num_necessary_parents] = parent_task;
            task->num_necessary_parents++;
        } /* end if */

        /* Release parent task mutex */
#ifdef AXE_DEBUG_LOCK
        printf("AXE_schedule_add_barrier: unlock task_mutex: %p\n", &parent_task->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
        if(0 != pthread_mutex_unlock(&parent_task->task_mutex))
            ERROR;
    } /* end for */

    /* Release task list mutex */
    if(0 != pthread_mutex_unlock(&schedule->task_list_mutex))
        ERROR;

    /* Finish adding the task to the schedule */
    if(AXE_schedule_add_common(task) != AXE_SUCCEED)
        ERROR;

done:
    return ret_value;
} /* end AXE_schedule_add_barrier() */


/*-------------------------------------------------------------------------
 * Function:    AXE_schedule_finish
 *
 * Purpose:     Updates the schedule to account for the specified task
 *              completing, and updates the task.  Updates all child
 *              tasks, enqueueing any which became schedulable as a
 *              result, signals waiting threads, then attempts to launch
 *              all tasks in the scheduled task queue until it runs out of
 *              tasks or runs out of threads.  If it runs out of threads
 *              first, it will return a dequeued task in *task, for
 *              execution in this thread.
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
AXE_schedule_finish(AXE_task_int_t **task/*in,out*/)
{
    AXE_task_int_t *child_task;
    AXE_task_int_t *free_list_head = NULL;
    AXE_task_int_t **free_list_tail_ptr = &free_list_head;
    AXE_schedule_t *schedule;
    AXE_thread_t *thread;
    AXE_status_t prev_status;
    size_t i;
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(task);
    assert(*task);

    schedule = (*task)->engine->schedule;

    /* Acquire task mutex while iterating over child arrays and changing state.
     * No other mutexes will be acquired while we hold this one. */
#ifdef AXE_DEBUG_LOCK
        printf("AXE_schedule_finish: lock task_mutex: %p\n", &(*task)->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
    if(0 != pthread_mutex_lock(&(*task)->task_mutex))
        ERROR;
        
    /* Mark as done, but only if it was not canceled.  Cache the previous status
     * so we know whether or not to send signals later. */
    prev_status = (AXE_status_t)OPA_cas_int(&(*task)->status, (int)AXE_TASK_RUNNING, (int)AXE_TASK_DONE);
#ifdef AXE_DEBUG
    if(prev_status == AXE_TASK_RUNNING)
        printf("AXE_schedule_finish: %p->status = AXE_TASK_DONE\n", *task); fflush(stdout);
#endif /* AXE_DEBUG */

    /* Update all necessary children */
    for(i = 0; i < (*task)->num_necessary_children; i++) {
        child_task = (*task)->necessary_children[i];

        /* If the parent task was canceled, we should cancel the child as well.
         * Check child status before calling AXE_schedule_cancel() so we don't
         * have to make the function call and lock the mutex if it's already
         * canceled. */
        if((prev_status == AXE_TASK_CANCELED)
                && ((AXE_status_t)OPA_load_int(&child_task->status)
                != AXE_TASK_CANCELED))
            if(AXE_schedule_cancel(child_task, NULL, FALSE) != AXE_SUCCEED)
                ERROR;

        /* Check if this was the last condition fulfilled for the child (i.e.
         * this is the last necessary parent, the sufficient condition is
         * fulfilled, and the task is initialized) */
        if(OPA_fetch_and_incr_int(&child_task->num_conditions_complete)
                == child_task->num_necessary_parents + 1) {
            /* The task can be scheduled - enqueue it */
            /* The fetch-and-incr should guarantee (along with similar
             * constructions elsewhere in this function and in AXE_schedule_add)
             * that only one thread ever sees the last condition fulfilled, but
             * we still need compare-and-swap in case this task has been
             * canceled.  Still enqueue the task if canceled, but leave marked
             * as canceled. */
            (void)OPA_cas_int(&child_task->status, (int)AXE_WAITING_FOR_PARENT, (int)AXE_TASK_SCHEDULED);

            assert(((AXE_status_t)OPA_load_int(&child_task->status) == AXE_TASK_SCHEDULED) || ((AXE_status_t)OPA_load_int(&child_task->status) == AXE_TASK_CANCELED));

            /* Write barrier to make sure the status is updated before
             * the task is scheduled */
            OPA_write_barrier();

#ifdef AXE_DEBUG
            printf("AXE_schedule_finish: enqueue %p nec\n", child_task); fflush(stdout);
#endif /* AXE_DEBUG */

            /* Add task to scheduled queue */
            OPA_Queue_enqueue(&schedule->scheduled_queue, child_task, AXE_task_int_t, scheduled_queue_hdr);
#ifdef AXE_DEBUG_NTASKS
            OPA_incr_int(&schedule->nenqueues);
#endif /* AXE_DEBUG_NTASKS */
        } /* end if */

        /* Decrement ref count on child.  Need to delay freeing the child if the
         * ref count drops to zero because we hold a task mutex and freeing the
         * task takes the task list mutex, which could cause a deadlock as some
         * functions take task mutexes while holding the task list mutex. */
#ifdef AXE_DEBUG_REF
        printf("AXE_schedule_finish: decr ref: %p nec", child_task); fflush(stdout);
#endif /* AXE_DEBUG_REF */
        (void)AXE_task_decr_ref(child_task, free_list_tail_ptr);

        /* Advance free_list_tail_ptr if it was set */
        if(*free_list_tail_ptr) {
            free_list_tail_ptr = &(*free_list_tail_ptr)->free_list_next;
            assert(!(*free_list_tail_ptr));
        } /* end if */
    } /* end for */

    /* Update all sufficient children */
    for(i = 0; i < (*task)->num_sufficient_children; i++) {
        child_task = (*task)->sufficient_children[i];

        /* Mark the sufficient condition as complete and check if this was the
         * first sufficient parent to complete for the child */
        if(OPA_swap_int(&child_task->sufficient_complete, TRUE) == FALSE) {
            /* If the parent task was canceled, we should cancel the child as
             * well.  Check child status before calling AXE_schedule_cancel() so
             * we don't have to make the function call and lock the mutex if
             * it's already canceled. */
            if((prev_status == AXE_TASK_CANCELED)
                    && ((AXE_status_t)OPA_load_int(&child_task->status)
                    != AXE_TASK_CANCELED))
                if(AXE_schedule_cancel(child_task, NULL, FALSE) != AXE_SUCCEED)
                    ERROR;

            /* Increment num_conditions_complete and check if this was the last
             * condition needed  (i.e. all necessary parents were complete and
             * the initialization is complete) */
            if(OPA_fetch_and_incr_int(&child_task->num_conditions_complete)
                == child_task->num_necessary_parents + 1) {
                /* The task can be scheduled - enqueue it */
                /* The fetch-and-incr should guarantee (along with similar
                 * constructions elsewhere in this function and in
                 * AXE_schedule_add) that only one thread ever sees the last
                 * condition fulfilled, but we still need compare-and-swap in
                 * case this task has been canceled.  Still enqueue the task if
                 * canceled, but leave marked as canceled. */
                (void)OPA_cas_int(&child_task->status, (int)AXE_WAITING_FOR_PARENT, (int)AXE_TASK_SCHEDULED);

                assert(((AXE_status_t)OPA_load_int(&child_task->status) == AXE_TASK_SCHEDULED) || ((AXE_status_t)OPA_load_int(&child_task->status) == AXE_TASK_CANCELED));

                /* Write barrier to make sure the status is updated before the
                 * task is scheduled */
                OPA_write_barrier();

#ifdef AXE_DEBUG
                printf("AXE_schedule_finish: enqueue %p suf\n", child_task); fflush(stdout);
#endif /* AXE_DEBUG */

                /* Add task to scheduled queue */
                OPA_Queue_enqueue(&schedule->scheduled_queue, child_task, AXE_task_int_t, scheduled_queue_hdr);
#ifdef AXE_DEBUG_NTASKS
                OPA_incr_int(&schedule->nenqueues);
#endif /* AXE_DEBUG_NTASKS */
            } /* end if */
        } /* end if */

        /* Decrement ref count on child.  Need to delay freeing the child if the
         * ref count drops to zero because we hold a task mutex and freeing the
         * task takes the task list mutex, which could cause a deadlock as some
         * functions take task mutexes while holding the task list mutex. */
#ifdef AXE_DEBUG_REF
        printf("AXE_schedule_finish: decr ref: %p suf", child_task); fflush(stdout);
#endif /* AXE_DEBUG_REF */
        (void)AXE_task_decr_ref(child_task, free_list_tail_ptr);

        /* Advance free_list_tail_ptr if it was set */
        if(*free_list_tail_ptr) {
            free_list_tail_ptr = &(*free_list_tail_ptr)->free_list_next;
            assert(!(*free_list_tail_ptr));
        } /* end if */
    } /* end for */

    /* Keep task mutex locked while we change status to done, so AXE_task_wait
     * knows that if the task is not marked done it is safe to wait on the
     * condition. */
    /* It should be possible to eliminate the signal broadcast unless a thread
     * actually needs it by using a field in the thread struct to keep track of
     * whether any threads are waiting on this task and careful ordering of
     * operations.  This function would first set the status to DONE, do a
     * read/write barrier, then check the waiting field. AXE_task_wait() would
     * first set the waiting field, do a read/write barrier, then check the task
     * status.  I am not sure how much faster this would be (if any) than the
     * simpler/more obvious implementation below.  A similar note applies to the
     * wait_all implementation as well.  -NAF */

    /* Send signals to threads waiting on this thread to complete (and possibly
     * those waiting on all threads), but only if it was not previously canceled
     * (if it was canceled then the signals were already sent). */
    if(prev_status == AXE_TASK_RUNNING) {
        /* Signal threads waiting on this task to complete */
        if(0 != pthread_cond_broadcast(&(*task)->wait_cond))
            ERROR;

        /* Unlock task mutex */
#ifdef AXE_DEBUG_LOCK
        printf("AXE_schedule_finish: unlock task_mutex: %p\n", &(*task)->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
        if(0 != pthread_mutex_unlock(&(*task)->task_mutex))
            ERROR;

#ifdef AXE_DEBUG_NTASKS
        OPA_incr_int(&schedule->ncomplete);
#endif /* AXE_DEBUG_NTASKS */

        /* Lock wait_all mutex */
        if(0 != pthread_mutex_lock(&schedule->wait_all_mutex))
            ERROR;

        /* Decrement the number of tasks and if this was the last task signal
         * threads waiting for all tasks to complete */
        if(OPA_decr_and_test_int(&schedule->num_tasks)) {
            /* Signal threads waiting on all tasks to complete */
            if(0 != pthread_cond_broadcast(&schedule->wait_all_cond))
                ERROR;
        } /* end if */

        /* Unlock wait_all mutex */
        if(0 != pthread_mutex_unlock(&schedule->wait_all_mutex))
            ERROR;
    } /* end if */
    else {
        assert(prev_status = AXE_TASK_CANCELED);

        /* Unlock task mutex */
#ifdef AXE_DEBUG_LOCK
        printf("AXE_schedule_finish: unlock task_mutex: %p\n", &(*task)->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
        if(0 != pthread_mutex_unlock(&(*task)->task_mutex))
            ERROR;
    } /* end else */

    /* Free tasks on the free list */
    while(free_list_head) {
        child_task = free_list_head;
        free_list_head = child_task->free_list_next;
        if(AXE_task_free(child_task) != AXE_SUCCEED)
            ERROR;
    } /* end while */

    /* Note: if we ever switch to a lockfree algorithm, we will need to add a
     * read/write barrier here to ensure consistency across client operator
     * tasks and to ensure that the status is updated before decrementing the
     * ref count */

    /* Decrement ref count - this task is complete and no longer part of the
     * schedule */
#ifdef AXE_DEBUG_REF
    printf("AXE_schedule_finish: decr ref: %p\n", *task); fflush(stdout);
#endif /* AXE_DEBUG_REF */
    if(AXE_task_decr_ref(*task, NULL) != AXE_SUCCEED)
        ERROR;

    /* This function must not use the supplied task after this point, as it
     * could be freed by the call to AXE_task_decr_ref() */
#ifndef NDEBUG
    *task = NULL;
#endif /* NDEBUG */

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
#ifdef NDEBUG
            OPA_decr_int(&schedule->sleeping_workers);
#else /* NDEBUG */
            assert(OPA_fetch_and_decr_int(&schedule->sleeping_workers) > 0);
#endif /* NDEBUG */

            /* Retrieve task from scheduled queue */
            OPA_Queue_dequeue(&schedule->scheduled_queue, *task, AXE_task_int_t, scheduled_queue_hdr);
#ifdef AXE_DEBUG_NTASKS
            OPA_incr_int(&schedule->ndequeues);
#endif /* AXE_DEBUG_NTASKS */
#ifdef AXE_DEBUG
            printf("AXE_schedule_finish: dequeue %p\n", *task); fflush(stdout);
#endif /* AXE_DEBUG */

            /* Unlock scheduled queue mutex */
            if(0 != pthread_mutex_unlock(&schedule->scheduled_queue_mutex))
                ERROR;

            assert(*task);

            /* Try to retrieve a thread from the thread pool */
            /* To prevent the race condition where a worker threads are past the
             * point where they look for tasks but have not yet been released to
             * the thread pool, delaying or preventing execution of this task,
             * loop until we either get a thread or get confirmation that all
             * threads are busy and will attempt to acquire a task when they
             * complete */
            do {
                /* Try to retrieve a thread from the thread pool */
                if(AXE_thread_pool_try_acquire((*task)->engine->thread_pool, &thread) != AXE_SUCCEED)
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
                    /* All workers are busy and the first one to finish will
                     * pick up this task.  We can go ahead and return. */
                    break;
                else {
                    /* If the schedule is closing, then there may be threads
                     * marked as "sleeping" that have been shut down.  In this
                     * case just return as it does not matter if tasks get
                     * ignored after we begin closing. */
                    if(OPA_load_int(&schedule->closing))
                        break;

                    /* The queue was empty but at least one worker was possibly
                     * soon to be finished.  Give the finishing workers a chance
                     * to finish. */
                    AXE_YIELD();
#ifdef AXE_DEBUG_PERF
                    OPA_incr_int(&AXE_debug_nspins_finish);
#endif /* AXE_DEBUG_PERF */
                } /* end else */
            } while(1);

            if(thread)
                /* Launch the task */
                if(AXE_thread_pool_launch(thread, AXE_task_worker, *task) != AXE_SUCCEED)
                    ERROR;
        } /* end else */
    } while(thread);

done:
    return ret_value;
} /* end AXE_schedule_finish() */


/*-------------------------------------------------------------------------
 * Function:    AXE_schedule_wait_all
 *
 * Purpose:     Blocks until all tasks in the specified schedule are
 *              either complete or canceled.
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
AXE_schedule_wait_all(AXE_schedule_t *schedule)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(schedule);

    /* Lock wait_all mutex */
    if(0 != pthread_mutex_lock(&schedule->wait_all_mutex))
        ERROR;

    /* Check if all tasks are already complete */
    if(OPA_load_int(&schedule->num_tasks) > 0)
        /* Wait for signal */
        if(0 != pthread_cond_wait(&schedule->wait_all_cond, &schedule->wait_all_mutex))
            ERROR;

    assert(OPA_load_int(&schedule->num_tasks) == 0);

    /* Unlock wait_all mutex */
    if(0 != pthread_mutex_unlock(&schedule->wait_all_mutex))
        ERROR;

done:
    return ret_value;
} /* end AXE_schedule_wait_all() */


/*-------------------------------------------------------------------------
 * Function:    AXE_schedule_cancel
 *
 * Purpose:     Attempts to cancel the specified task.  The result of the
 *              attempt is returned in *remove_status.  Sends signals to
 *              waiting threads as appropriate.  If the caller hold the
 *              task mutex for task, have_task_mutex should be set to
 *              TRUE, otherwise have_task_mutex should be set to FALSE.
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
AXE_schedule_cancel(AXE_task_int_t *task, AXE_remove_status_t *remove_status,
    _Bool have_task_mutex)
{
    AXE_schedule_t *schedule;
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(task);

    schedule = task->engine->schedule;

    /* Lock task mutex if we do not already have it */
    if(!have_task_mutex) {
#ifdef AXE_DEBUG_LOCK
        printf("AXE_schedule_cancel: lock task_mutex: %p\n", &task->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
        if(0 != pthread_mutex_lock(&task->task_mutex))
            ERROR;
    } /* end if */

    /* Try to mark the task canceled.  Only send the signal if we succeed. */
    if(((AXE_status_t)OPA_cas_int(&task->status, (int)AXE_WAITING_FOR_PARENT,
              (int)AXE_TASK_CANCELED) == AXE_WAITING_FOR_PARENT)
            || ((AXE_status_t)OPA_cas_int(&task->status,
              (int)AXE_TASK_SCHEDULED, (int)AXE_TASK_CANCELED)
              == AXE_TASK_SCHEDULED)) {
        /* The task was canceled */
        if(remove_status)
            *remove_status = AXE_CANCELED;

#ifdef AXE_DEBUG_NTASKS
        OPA_incr_int(&schedule->ncancels);
#endif /* AXE_DEBUG_NTASKS */

        /* Signal threads waiting on this task to complete */
        if(0 != pthread_cond_broadcast(&task->wait_cond))
            ERROR;

        /* Unlock task mutex */
#ifdef AXE_DEBUG_LOCK
        printf("AXE_schedule_cancel: unlock task_mutex: %p\n", &task->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
        if(0 != pthread_mutex_unlock(&task->task_mutex))
            ERROR;

        /* Lock wait_all mutex */
        if(0 != pthread_mutex_lock(&schedule->wait_all_mutex))
            ERROR;

        /* Decrement the number of tasks and if this was the last task signal
         * threads waiting for all tasks to complete */
        if(OPA_decr_and_test_int(&schedule->num_tasks)) {
            /* Signal threads waiting on all tasks to complete */
            if(0 != pthread_cond_broadcast(&schedule->wait_all_cond))
                ERROR;
        } /* end if */

        /* Unlock wait_all mutex */
        if(0 != pthread_mutex_unlock(&schedule->wait_all_mutex))
            ERROR;
    } /* end if */
    else {
        /* Unlock task mutex */
#ifdef AXE_DEBUG_LOCK
        printf("AXE_schedule_cancel: unlock task_mutex: %p\n", &task->task_mutex); fflush(stdout);
#endif /* AXE_DEBUG_LOCK */
        if(0 != pthread_mutex_unlock(&task->task_mutex))
            ERROR;

        /* Check task status.  Okay to do here because canceled and done tasks
         * never change status, and running tasks only become done.  If a task
         * finished during execution it's reasonable to return AXE_ALL_DONE.  If
         * the task was already canceled, return AXE_ALL_DONE (for now). */
        if(remove_status) {
            if((AXE_status_t)OPA_load_int(&task->status) == AXE_TASK_RUNNING)
                *remove_status = AXE_NOT_CANCELED;
            else
                *remove_status = AXE_ALL_DONE;
        } /* end if */
    } /* end else */

done:
    return ret_value;
} /* end AXE_schedule_cancel */


/*-------------------------------------------------------------------------
 * Function:    AXE_schedule_cancel_all
 *
 * Purpose:     Attempts to cancel all tasks in the specified schedule.
 *              The result of the attempt is returned in *remove_status.
 *              Sends signals to waiting threads as appropriate.
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
AXE_schedule_cancel_all(AXE_schedule_t *schedule,
    AXE_remove_status_t *remove_status)
{
    AXE_task_int_t *task;
    AXE_remove_status_t task_remove_status;
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(schedule);

#ifdef AXE_DEBUG_NTASKS
    printf("sca: nadds: %d, nenqs: %d, ndqs: %d, ncmplt: %d, ncanc: %d ntasks: %d\n", OPA_load_int(&schedule->nadds), OPA_load_int(&schedule->nenqueues), OPA_load_int(&schedule->ndequeues), OPA_load_int(&schedule->ncomplete), OPA_load_int(&schedule->ncancels), OPA_load_int(&schedule->num_tasks));
#endif /* AXE_DEBUG_NTASKS */

    /* Start remove_status as AXE_ALL_DONE, so the first task sets remove_status
     * to its remove status */
    if(remove_status)
        *remove_status = AXE_ALL_DONE;

    /* Lock task list mutex */
    if(0 != pthread_mutex_lock(&schedule->task_list_mutex))
        ERROR;

    /* Loop over all tasks in the task list, marking all that are not running or
     * done as canceled */
    for(task = schedule->task_list_head.task_list_next;
            task != &schedule->task_list_tail;
            task = task->task_list_next) {
        assert(task->engine->schedule == schedule);

        /* Cancel task */
        if(AXE_schedule_cancel(task, &task_remove_status, FALSE) != AXE_SUCCEED)
            ERROR;

        /* Update remove_status */
        if(remove_status && ((*remove_status == AXE_ALL_DONE)
                || ((*remove_status == AXE_CANCELED)
                  && (task_remove_status == AXE_NOT_CANCELED))))
            *remove_status = task_remove_status;
    } /* end for */

    /* Unlock task list mutex */
    if(0 != pthread_mutex_unlock(&schedule->task_list_mutex))
        ERROR;

done:
    return ret_value;
} /* end AXE_schedule_cancel_all() */


/*-------------------------------------------------------------------------
 * Function:    AXE_schedule_remove_from_list
 *
 * Purpose:     Removes the specified task from the task list.  Should
 *              only be called when freeing the task, otherwise the
 *              library may not be able to clean it up when terminating
 *              the engine.  Must *not* be called while holding a task
 *              mutex!.
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
AXE_schedule_remove_from_list(AXE_task_int_t *task)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(task);

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
} /* end AXE_schedule_remove_from_list() */


/*-------------------------------------------------------------------------
 * Function:    AXE_schedule_closing
 *
 * Purpose:     Marks the specified schedule as "closing", preventing the
 *              scheduler from going out of its way to guarantee thread
 *              availability (which may not be possible because some
 *              threads may have shut down).
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
void
AXE_schedule_closing(AXE_schedule_t *schedule)
{
    assert(schedule);

    OPA_store_int(&schedule->closing, TRUE);

    return;
} /* end AXE_schedule_closing() */


/*-------------------------------------------------------------------------
 * Function:    AXE_schedule_free
 *
 * Purpose:     Frees the specified schedule and all tasks it contains.
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
AXE_schedule_free(AXE_schedule_t *schedule)
{
    AXE_task_int_t *task;
    AXE_task_int_t *next;
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(schedule);

#ifdef AXE_DEBUG_NTASKS
    printf("nadds: %d, nenqs: %d, ndqs: %d, ncmplt: %d, ncanc: %d\n", OPA_load_int(&schedule->nadds), OPA_load_int(&schedule->nenqueues), OPA_load_int(&schedule->ndequeues), OPA_load_int(&schedule->ncomplete), OPA_load_int(&schedule->ncancels));
#endif /* AXE_DEBUG_NTASKS */

    /* Free all remaining tasks.  They should all be done or canceled. */
    for(task = schedule->task_list_head.task_list_next;
            task != &schedule->task_list_tail;
            task = next) {
        assert(((AXE_status_t)OPA_load_int(&task->status) == AXE_TASK_CANCELED) || ((AXE_status_t)OPA_load_int(&task->status) == AXE_TASK_DONE));

        /* Cache next task because task will be freed */
        next = task->task_list_next;

        /* Set task_list_next pointer to NULL so AXE_task_free() doesn't bother
         * calling AXE_schedule_remove_task() */
        task->task_list_next = NULL;

        /* Free task */
        AXE_task_free(task);
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
#ifndef NDEBUG
    memset(schedule, 0, sizeof(*schedule));
#endif /* NDEBUG */
    free(schedule);

done:
    return ret_value;
} /* end AXE_schedule_free() */


/*-------------------------------------------------------------------------
 * Function:    AXE_schedule_add_common
 *
 * Purpose:     Code needed by both AXE_schedule_add() and
 *              AXE_schedule_add_barrier() to add a task to the schedule.
 *              Add the task to the task list, and if it can be run
 *              immediately, attempts to dequeue and run a single task.
 *              If no threads are available, the order of tasks in the
 *              scheduled queue is maintained.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
static AXE_error_t
AXE_schedule_add_common(AXE_task_int_t *task)
{
    AXE_schedule_t *schedule;
    AXE_thread_pool_t *thread_pool;
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(task);
    assert(task->engine);

    schedule = task->engine->schedule;
    thread_pool = task->engine->thread_pool;

    /* Increment the number of tasks */
    OPA_incr_int(&schedule->num_tasks);
#ifdef AXE_DEBUG_NTASKS
    OPA_incr_int(&schedule->nadds);
#endif /* AXE_DEBUG_NTASKS */
#ifdef AXE_DEBUG_PERF
    OPA_incr_int(&AXE_debug_nadds);
#endif /* AXE_DEBUG_PERF */

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

    /* The task handle can be safely used by the application at this point */

    /* Increment num_conditions_complete to account for initialization being
     * complete. Schedule the event if all necessary parents and at least one
     * sufficient parent are complete. */
    if((size_t)OPA_fetch_and_incr_int(&task->num_conditions_complete)
                == task->num_necessary_parents + 1) {
        AXE_thread_t *thread = NULL;
        AXE_task_int_t *exec_task = NULL;

        /* The fetch-and-incr should guarantee (along with similar constructions
         * in AXE_schedule_finish) that only one thread ever sees the last
         * condition fulfilled, however it is still possible (though unlikely)
         * for another thread to have canceled this task by now, so do a compare
         * -and-swap to preserve the canceled status in this case */
        (void)OPA_cas_int(&task->status, (int)AXE_WAITING_FOR_PARENT, (int)AXE_TASK_SCHEDULED);

        assert(((AXE_status_t)OPA_load_int(&task->status) == AXE_TASK_SCHEDULED) || ((AXE_status_t)OPA_load_int(&task->status) == AXE_TASK_CANCELED));

        /* Write barrier so we know that all changes to the task struct are
         * visible before we enqueue this task and subject it to being picked up
         * by a worker thread.  This is not necessary if this task is not
         * scheduled because the only ways this task could be reached again
         * involve taking mutexes (either through this function or
         * AXE_schedule_finish()). */
        OPA_write_barrier();

#ifdef AXE_DEBUG
        printf("AXE_schedule_add: enqueue %p\n", task); fflush(stdout);
#endif /* AXE_DEBUG */

        /* Add task to scheduled queue */
        OPA_Queue_enqueue(&schedule->scheduled_queue, task, AXE_task_int_t, scheduled_queue_hdr);
#ifdef AXE_DEBUG_NTASKS
        OPA_incr_int(&schedule->nenqueues);
#endif /* AXE_DEBUG_NTASKS */

        /* This function must not use task after this point, as it could be
         * executed (and freed) by another thread at any point after we enqueue
         * it */
#ifndef NDEBUG
        task = NULL;
#endif /* NDEBUG */

        /*
         * Now try to execute the event
         */
        /* Note that we only try to take one task from the queue in this
         * function because we only pushed one onto the queue.  If there is more
         * than one task in the queue, then either the thread pool is full or
         * there is another thread scheduling tasks which will execute at least
         * as many as it pushes.  In either case there is no need to pull more
         * than one thread. */
        /* To prevent the race condition where a worker threads are past the
         * point where they look for tasks but have not yet been released to the
         * thread pool, delaying or preventing execution of this task, loop
         * until we either get a thread or get confirmation that all threads are
         * busy and will attempt to acquire a task when they complete */
        do {
            /* Try to retrieve a thread from the thread pool */
            if(AXE_thread_pool_try_acquire(thread_pool, &thread) != AXE_SUCCEED)
                ERROR;

            /* If we have a thread we can exit */
            if(thread)
                break;

            /* Read barrier so the check on sleeping_workers happens after
             * the failed acquire */
            OPA_read_barrier();

            /* Check if all workers are busy and guaranteed to check the
             * schedule before sleeping */
            if(OPA_load_int(&schedule->sleeping_workers) == 0)
                /* All workers are busy and the first one to finish will pick up
                 * this task.  We can go ahead and return. */
                break;
            else {
                /* If the schedule is closing, then there may be threads marked
                 * as "sleeping" that have been shut down.  In this case just
                 * return as it does not matter if tasks get ignored after we
                 * begin closing. */
                if(OPA_load_int(&schedule->closing))
                    break;

                /* The queue was empty but at least one worker was possibly soon
                 * to be finished.  Give the finishing workers a chance to
                 * finish. */
                AXE_YIELD();
#ifdef AXE_DEBUG_PERF
                OPA_incr_int(&AXE_debug_nspins_add);
#endif /* AXE_DEBUG_PERF */
            } /* end else */
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
                AXE_thread_pool_release(thread);
                thread = NULL;
            } /* end if */
            else {
                /* Retrieve task from scheduled queue */
                OPA_Queue_dequeue(&schedule->scheduled_queue, exec_task, AXE_task_int_t, scheduled_queue_hdr);
#ifdef AXE_DEBUG_NTASKS
                OPA_incr_int(&schedule->ndequeues);
#endif /* AXE_DEBUG_NTASKS */
#ifdef AXE_DEBUG
printf("AXE_schedule_add: dequeue %p\n", exec_task); fflush(stdout);
#endif /* AXE_DEBUG */

                /* Unlock scheduled queue mutex */
                if(0 != pthread_mutex_unlock(&schedule->scheduled_queue_mutex))
                    ERROR;

                assert(exec_task);

                /* Launch the task */
                if(AXE_thread_pool_launch(thread, AXE_task_worker, exec_task) != AXE_SUCCEED)
                    ERROR;
            } /* end else */
        } /* end if */
    } /* end if */

done:
    return ret_value;
} /* end AXE_schedule_add_common() */

