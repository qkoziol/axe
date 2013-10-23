/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "AXEthreadpool.h"


/*
 * Local typedefs
 */
/* Thread pool structure */
struct AXE_thread_pool_t {
    OPA_Queue_info_t        thread_queue;       /* Queue of threads available to be run */
    pthread_mutex_t         thread_queue_mutex; /* Mutex for dequeueing from thread_queue, and checking thread_wait_cond */
    pthread_attr_t          thread_attr;        /* Pthread attribute for thread creation */
    OPA_int_t               closing;            /* Boolean variable indicating if the thread pool is shutting down.  Needed to prevent race conditions. */
    OPA_int_t               num_sleeping_threads; /* Number of threads that are enqueued or may be enqueued soon and should be considered "available" */
    _Bool                   exclusive_waiter;   /* Whether an "exclusive" thread is waiting */
    int                     num_waiters;        /* Number of threads waiting for a thread */
    pthread_cond_t          thread_wait_cond;   /* Condition variable for waiting for thread */
    size_t                  num_threads;        /* Number of threads */
    AXE_thread_t            **threads;          /* Array of thread structs */
};

/* Thread structure */
struct AXE_thread_t {
    OPA_Queue_element_hdr_t thread_queue_hdr;   /* Header for insertion into thread pool's "thread_queue" */
    AXE_thread_pool_t       *thread_pool;       /* The thread pool this thread resides in */
    pthread_cond_t          thread_cond;        /* Condition variable for signaling this thread to run */
    pthread_mutex_t         thread_mutex;       /* Mutex associated with thread_cond */
    AXE_thread_op_t         thread_op;          /* Internal callback function for thread to execute */
    void                    *thread_op_data;    /* User data pointer passed to thread_op */
    pthread_t               thread_info;        /* Thread handle for use with pthread_join() */
};


/*
 * Local functions
 */
static void *AXE_thread_pool_worker(void *_thread);


/*-------------------------------------------------------------------------
 * Function:    AXE_thread_pool_create
 *
 * Purpose:     Creates a new thread pool.  Allocates and initializes the
 *              thread pool, then creates and launches all threads.
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
AXE_thread_pool_create(size_t num_threads,
    AXE_thread_pool_t **thread_pool/*out*/)
{
    AXE_thread_t *thread;
    _Bool is_thread_mutex_init = FALSE;
    _Bool is_thread_attr_init = FALSE;
    _Bool is_thread_wait_cond_init = FALSE;
    AXE_error_t ret_value = AXE_SUCCEED;

    *thread_pool = NULL;

    /* Allocate thread pool */
    if(NULL == (*thread_pool = (AXE_thread_pool_t *)malloc(sizeof(AXE_thread_pool_t))))
        ERROR;

    /* Initialize malloc'd fields to NULL, so they are not freed if something
     * goes wrong */
    (*thread_pool)->num_threads = 0;
    (*thread_pool)->threads = NULL;

    /* Initialize thread queue */
    OPA_Queue_init(&(*thread_pool)->thread_queue);

    /* Initialize queue mutex */
    if(0 != pthread_mutex_init(&(*thread_pool)->thread_queue_mutex, NULL))
        ERROR;
    is_thread_mutex_init = TRUE;

    /* Initialize thread attribute */
    if(0 != pthread_attr_init(&(*thread_pool)->thread_attr))
        ERROR;
    is_thread_attr_init = TRUE;

    /* Set threads to detached */
    if(0 != pthread_attr_setdetachstate(&(*thread_pool)->thread_attr, PTHREAD_CREATE_JOINABLE))
        ERROR;

    /* Initialize closing field */
    OPA_store_int(&(*thread_pool)->closing, FALSE);

    /* Initialize num_sleeping_threads, exclusive_waiter, and num_waiters */
    OPA_store_int(&(*thread_pool)->num_sleeping_threads, (int)num_threads);
    (*thread_pool)->exclusive_waiter = FALSE;
    (*thread_pool)->num_waiters = 0;

    /* Initialize thread_wait_cond */
    if(0 != pthread_cond_init(&(*thread_pool)->thread_wait_cond, NULL))
        ERROR;
    is_thread_wait_cond_init = TRUE;

    /* Allocate threads array */
    if(NULL == ((*thread_pool)->threads = (AXE_thread_t **)malloc(num_threads * sizeof(AXE_thread_t *))))
        ERROR;

    /* Create threads */
    for((*thread_pool)->num_threads = 0;
            (*thread_pool)->num_threads < num_threads;
            (*thread_pool)->num_threads++) {
        /* Allocate thread */
        if(NULL == ((*thread_pool)->threads[(*thread_pool)->num_threads] = (AXE_thread_t *)malloc(sizeof(AXE_thread_t))))
            ERROR;

        /* Set convenience variable */
        thread = (*thread_pool)->threads[(*thread_pool)->num_threads];

        /* Initialize thread */
        OPA_Queue_header_init(&thread->thread_queue_hdr);
        thread->thread_pool = *thread_pool;
        if(0 != pthread_cond_init(&thread->thread_cond, NULL))
            ERROR;
        if(0 != pthread_mutex_init(&thread->thread_mutex, NULL))
            ERROR;
        thread->thread_op = NULL;
        thread->thread_op_data = NULL;

        /* Launch thread */
        if(0 != pthread_create(&thread->thread_info, &(*thread_pool)->thread_attr, AXE_thread_pool_worker, thread))
            ERROR;
    } /* end for */

    assert((*thread_pool)->num_threads == num_threads);

done:
    if(ret_value == AXE_FAIL)
        if(*thread_pool) {
            /* Cleanup on error - if we have already launched at least one
             * thread, use normal free routine */
            if((*thread_pool)->num_threads > 0)
                (void)AXE_thread_pool_free(*thread_pool);
            else {
                if(is_thread_attr_init)
                    (void)pthread_attr_destroy(&(*thread_pool)->thread_attr);
                if(is_thread_mutex_init)
                    (void)pthread_mutex_destroy(&(*thread_pool)->thread_queue_mutex);
                if(is_thread_wait_cond_init)
                    (void)pthread_cond_destroy(&(*thread_pool)->thread_wait_cond);
                if((*thread_pool)->threads)
                    free((*thread_pool)->threads);
                free(*thread_pool);
            } /* end else */
            *thread_pool = NULL;
        } /* end if */

    return ret_value;
} /* end AXE_thread_pool_create() */


/*-------------------------------------------------------------------------
 * Function:    AXE_thread_pool_try_acquire
 *
 * Purpose:     Attempts to acquire a thread for later use with
 *              AXE_thread_pool_launch() or  AXE_thread_pool_release().
 *              If a thread was acquired it is returned in *thread.
 *
 *              If the thread queue is empty but there are threads
 *              marked "sleeping" (may be enqueued soon), this thread
 *              waits until signaled that either a thread has been
 *              enqueued or no threads are sleeping.
 *
 *              If exclusive_waiter is set to TRUE, then if this thread
 *              waits other threads are prevented from waiting (though
 *              they are not woken up if they were already waiting).
 *
 *              We use try_acquire() followed by launch() instead of just
 *              try_launch() to avoid interfering with the order of
 *              scheduled tasks or having to take multiple mutexes at the
 *              same time, which makes it more difficult to prove that the
 *              algorithm cannot deadlock.
 *
 *              It does not matter if the threads change position in the
 *              queue (as happens if the caller fails to acquire a task),
 *              but it is not ideal (with respect to fairness in
 *              scheduling) for tasks to be reordered when the caller
 *              fails to acquire a thread.  Thus we pop a thread first,
 *              and push it back if we cannot get a task, and not the
 *              other way around.
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
AXE_thread_pool_try_acquire(AXE_thread_pool_t *thread_pool,
    _Bool exclusive_waiter, AXE_thread_t **thread/*out*/)
{
    int is_empty;
    _Bool waiting = FALSE;
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(thread_pool);
    assert(thread);

    /* Lock the thread queue mutex - only one thread can dequeue at a time with
     * the current queue implementation */
    /* Note that the thread queue mutex is always unlocked shortly afterwards
     * without any intervening locks.  Therefore, it will not cause a deadlock.
     */
    if(0 != pthread_mutex_lock(&thread_pool->thread_queue_mutex))
        ERROR;

    /* Check if the queue is empty.  If so, and if there are threads marked as
     * "sleeping" and the thread pool is not closing, wait until we are signaled
     * that one of these conditions changes.  Note that OPA_Queue_is_empty() is
     * always called after pthread_cond_wait(), so the mutex lock is not
     * interrupted between the last call to OPA_Queue_is_empty() and the call to
     * OPA_Queue_dequeue(). */
    while((is_empty = OPA_Queue_is_empty(&thread_pool->thread_queue))
            && (OPA_load_int(&thread_pool->num_sleeping_threads) > 0)
            && !OPA_load_int(&thread_pool->closing)) {
        /* Check to see if we should give up due to an exclusive waiter, and
         * set the thread pool's exclusive_waiter flag if appropriate.  Also
         * increment the number of waiters so signals get sent. */
        if(!waiting) {
            if(thread_pool->exclusive_waiter)
                break;
            if(exclusive_waiter) {
                thread_pool->exclusive_waiter = TRUE;
            } /* end if */
            waiting = TRUE;
            thread_pool->num_waiters++;
        } /* end if */

        /* Queue is empty and threads should soon become available.  Wait for
         * signal. */
        if(0 != pthread_cond_wait(&thread_pool->thread_wait_cond, &thread_pool->thread_queue_mutex))
            ERROR;
    } /* end while */

    /* Reset exclusive_waiter flag and num_waiters */
    if(waiting) {
        if(exclusive_waiter)
            thread_pool->exclusive_waiter = FALSE;
        thread_pool->num_waiters--;
    } /* end if */

    /* If the queue is empty set *thread to NULL, otherwise dequeue a thread */
    if(is_empty)
        *thread = NULL;
    else {
        /* Dequeue waiting thread */
        OPA_Queue_dequeue(&thread_pool->thread_queue, *thread, AXE_thread_t, thread_queue_hdr);
        assert(*thread);
    } /* end else */

    /* Unlock thread queue mutex */
    if(0 != pthread_mutex_unlock(&thread_pool->thread_queue_mutex))
        ERROR;

done:
    return ret_value;
} /* end AXE_thread_pool_try_acqure */


/*-------------------------------------------------------------------------
 * Function:    AXE_thread_pool_release
 *
 * Purpose:     Adds the specified thread back to the thread queue and
 *              signals a process waiting for a thread.
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
AXE_thread_pool_release(AXE_thread_t *thread)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(thread);

    /* Push thread back onto free thread queue */
    OPA_Queue_enqueue(&thread->thread_pool->thread_queue, thread, AXE_thread_t, thread_queue_hdr);

    /* Lock thread queue mutex */
    if(0 != pthread_mutex_lock(&thread->thread_pool->thread_queue_mutex))
        ERROR;

    /* Send signal that a new thread is available, if any threads are waiting
     * for the signal */
    if(thread->thread_pool->num_waiters > 0)
        if(0 != pthread_cond_signal(&thread->thread_pool->thread_wait_cond))
            ERROR;

    /* Unlock thread queue mutex */
    if(0 != pthread_mutex_unlock(&thread->thread_pool->thread_queue_mutex))
        ERROR;

done:
    return ret_value;
} /* end AXE_thread_pool_release() */


/*-------------------------------------------------------------------------
 * Function:    AXE_thread_pool_running
 *
 * Purpose:     Inform the thread pool that the thread is running and may
 *              not return soon.  If this was the last sleeping thread,
 *              sends a signal to threads waiting for a thread that they
 *              need not wait any longer, as none will be available soon.
 *
 *              This function, together with AXE_thread_pool_sleeping(),
 *              allows the client to guarantee concurrent availability of
 *              num_threads threads.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              March 11, 2013
 *
 *-------------------------------------------------------------------------
 */
AXE_error_t
AXE_thread_pool_running(AXE_thread_pool_t *thread_pool)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(thread_pool);

    /* Decrement the number of sleeping threads, and if this was the last
     * sleeping thread broadcast signal that no new threads will become
     * available soon */
    if(OPA_decr_and_test_int(&thread_pool->num_sleeping_threads)) {
        /* Lock thread queue mutex */
        if(0 != pthread_mutex_lock(&thread_pool->thread_queue_mutex))
            ERROR;

        /* Send the signal if there are any threads waiting for it */
        if(thread_pool->num_waiters > 0)
            if(0 != pthread_cond_broadcast(&thread_pool->thread_wait_cond))
                ERROR;

        /* Unlock thread queue mutex */
        if(0 != pthread_mutex_unlock(&thread_pool->thread_queue_mutex))
            ERROR;
    } /* end if */

done:
    return ret_value;
} /* end AXE_thread_pool_running() */


/*-------------------------------------------------------------------------
 * Function:    AXE_thread_pool_sleeping
 *
 * Purpose:     Inform the thread pool that the thread is sleeping and may
 *              return soon.  The client should call this at the point it
 *              wants other threads requesting a thread to wait until this
 *              thread returns.  This call may be reversed by a subsequent
 *              call to AXE_thread_pool_running(), useful if a thread no
 *              longer needs to be waited for.
 *
 *              This function, together with AXE_thread_pool_running(),
 *              allows the client to guarantee concurrent availability of
 *              num_threads threads.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              March 11, 2013
 *
 *-------------------------------------------------------------------------
 */
void
AXE_thread_pool_sleeping(AXE_thread_pool_t *thread_pool)
{
    assert(thread_pool);

    /* Increment the number of sleeping threads */
    OPA_incr_int(&thread_pool->num_sleeping_threads);

    return;
} /* end AXE_thread_pool_sleeping() */


/*-------------------------------------------------------------------------
 * Function:    AXE_thread_pool_launch
 *
 * Purpose:     Uses the specified thread to launch the client operator
 *              thread_op with client data thread_op_data.
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
AXE_thread_pool_launch(AXE_thread_t *thread, AXE_thread_op_t thread_op,
    void *thread_op_data)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    assert(thread);
    assert(thread_op);

    /* Lock thread mutex */
    if(0 != pthread_mutex_lock(&thread->thread_mutex))
        ERROR;

    /* If the thread pool is closing, do not attempt to launch thread, simply
     * return.  Not running the task is ok since we are closing down. */
    if(!OPA_load_int(&thread->thread_pool->closing)) {
        /* Add operator info to thread struct */
        thread->thread_op = thread_op;
        thread->thread_op_data = thread_op_data;

        /* Send condition signal to wake up thread */
        if(0 != pthread_cond_signal(&thread->thread_cond))
            ERROR;
    } /* end if */

    /* Unlock the thread mutex to allow the thread to proceed */
    if(0 != pthread_mutex_unlock(&thread->thread_mutex))
        ERROR;

done:
    return ret_value;
} /* end AXE_thread_pool_launch() */


/*-------------------------------------------------------------------------
 * Function:    AXE_thread_pool_free
 *
 * Purpose:     Frees the specified thread pool.  First signals all
 *              threads to shut down, joins all threads, and frees all
 *              thread structs.
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
AXE_thread_pool_free(AXE_thread_pool_t *thread_pool)
{
    AXE_thread_t *thread;
    size_t i;
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Mark thread pool as closing, to prevent rare race condition */
    OPA_store_int(&thread_pool->closing, TRUE);

    /* Lock thread queue mutex */
    if(0 != pthread_mutex_lock(&thread_pool->thread_queue_mutex))
        ERROR;

    /* Signal that the thread pool is closing, if any threads are waiting for a
     * thread to become available */
    if(thread_pool->num_waiters > 0)
        if(0 != pthread_cond_broadcast(&thread_pool->thread_wait_cond))
            ERROR;

    /* Unlock thread queue mutex */
    if(0 != pthread_mutex_unlock(&thread_pool->thread_queue_mutex))
        ERROR;

    /* Note no memory barrier is necessary here because of the mutexes */

    /* Shut down all threads */
    for(i = 0; i < thread_pool->num_threads; i++) {
        thread = thread_pool->threads[i];

        /* Lock thread mutex */
        if(0 != pthread_mutex_lock(&thread->thread_mutex))
            ERROR;

        /* It is possible for these to not be NULL if a thread attempting to
         * launch this thread acquired the mutex first and then this thread
         * acquired it before the worker.  Since we are shutting down, there
         * is no harm in overriding the attempted launch of the (canceled)
         * task with a shutdown request. */
        thread->thread_op = NULL;
        thread->thread_op_data = NULL;

        /* Send condition signal to wake up thread.  Because thread_op is NULL,
         * the thread will terminate.  The thread will release its own
         * resources. */
        if(0 != pthread_cond_signal(&thread->thread_cond))
            ERROR;

        /* Unlock the thread mutex to allow the thread to proceed */
        if(0 != pthread_mutex_unlock(&thread->thread_mutex))
            ERROR;

        /* Join the thread */
        if(0 != pthread_join(thread->thread_info, NULL))
            ERROR;
    } /* end for */

    /* Destroy all threads.  Once we get here we know the queue will no longer
     * be accessed, so it is safe to free the threads even if they are still in
     * the queue (no need to empty the queue). */
    for(i = 0; i < thread_pool->num_threads; i++) {
        thread = thread_pool->threads[i];

        /* Destroy thread mutex */
        if(0 != pthread_mutex_destroy(&thread->thread_mutex))
            ERROR;

        /* Destroy thread condition variable */
        if(0 != pthread_cond_destroy(&thread->thread_cond))
            ERROR;

#ifdef AXE_DEBUG
        printf("AXE_thread_pool_free: free thread %p\n", thread); fflush(stdout);
#endif /* AXE_DEBUG */

        /* Free the thread */
#ifndef NDEBUG
        memset(thread, 0, sizeof(*thread));
#endif /* NDEBUG */
        free(thread);
    } /* end for */

    /* Destroy queue mutex */
    if(0 != pthread_mutex_destroy(&thread_pool->thread_queue_mutex))
        ERROR;

    /* Destroy thread attribute */
    if(0 != pthread_attr_destroy(&thread_pool->thread_attr))
        ERROR;

    /* Destroy thread wait condition variable */
    if(0 != pthread_cond_destroy(&thread_pool->thread_wait_cond))
        ERROR;

    /* Free threads array */
    free(thread_pool->threads);

    /* Free schedule */
#ifndef NDEBUG
    memset(thread_pool, 0, sizeof(*thread_pool));
#endif /* NDEBUG */
    free(thread_pool);

done:
    return ret_value;
} /* end AXE_thread_pool_free() */


/*-------------------------------------------------------------------------
 * Function:    AXE_thread_pool_worker
 *
 * Purpose:     Internal thread pool worker routine.  Repeatedly waits
 *              until signaled to run, and executes the callback function
 *              placed in its thread struct until it is signaled to run
 *              without a provided callback, at which point it returns.
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
static void *
AXE_thread_pool_worker(void *_thread)
{
    AXE_thread_t *thread = (AXE_thread_t *)_thread;
    void *ret_value = NULL;

    assert(thread);

    /* Lock the thread mutex - this will only be unlocked by pthread_cond_wait
     */
    if(0 != pthread_mutex_lock(&thread->thread_mutex))
        ERROR_RET(thread);

    /* Check if the thread pool is shutting down (to prevent the race condition
     * where the thread pool already sent the signal to shut down before this
     * thread locked its mutex) */
    if(!OPA_load_int(&thread->thread_pool->closing)) {
        /* Push thread onto free thread queue */
        if(AXE_thread_pool_release(thread) != AXE_SUCCEED)
            ERROR_RET(thread);

        /* Wait until signaled to run */
        if(0 != pthread_cond_wait(&thread->thread_cond, &thread->thread_mutex))
            ERROR_RET(thread);
    } /* end if */
    else
        assert(!thread->thread_op);

    /* Main loop - when thread_op is set to NULL, shut down */
    while(thread->thread_op) {
        /* Mark thread as running */
        if(AXE_thread_pool_running(thread->thread_pool) != AXE_SUCCEED)
            ERROR_RET(thread);

        /* Launch client operator */
        if(thread->thread_op(thread->thread_op_data) != AXE_SUCCEED)
            ERROR_RET(thread);

        /* Reset operator */
        thread->thread_op = NULL;
        thread->thread_op_data = NULL;

        /* Push thread back onto free thread queue */
        if(AXE_thread_pool_release(thread) != AXE_SUCCEED)
            ERROR_RET(thread);

        /* Note: The thread must never take any mutexes (except the thread
         * mutex, which it immediately releases in pthread_cond_wait) while on
         * the thread queue.  In other words, a wait on the thead mutex for a
         * dequeued thread is guaranteed to succeed before the dequeued thread
         * attempts to lock any mutex.  This is to prevent deadlocks. */

        /* Wait until signaled to run again */
        if(0 != pthread_cond_wait(&thread->thread_cond, &thread->thread_mutex))
            ERROR_RET(thread);
    } /* end while */

done:
    /* Release thread mutex */
    if(0 != pthread_mutex_unlock(&thread->thread_mutex))
        ret_value = thread;

#ifdef AXE_DEBUG
    printf("AXE_thread_pool_worker exiting...\n"); fflush(stdout);
#endif /* AXE_DEBUG */

    pthread_exit(ret_value);
} /* end AXE_thread_pool_worker() */

