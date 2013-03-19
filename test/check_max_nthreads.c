/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include "axe_test.h"


/*-------------------------------------------------------------------------
 * Function:    check_max_nthreads_helper
 *
 * Purpose:     Blocks on the mutex supplied through the argument, then
 *              returns.
 *
 * Return:      Success: NULL
 *              Failure: (void *)1
 *
 * Programmer:  Neil Fortner
 *              March 18, 2013
 *
 *-------------------------------------------------------------------------
 */
void *
check_max_nthreads_helper(void *_mutex)
{
    pthread_mutex_t *mutex = (pthread_mutex_t *)_mutex;

    if(0 != pthread_mutex_lock(mutex))
        goto error;
    if(0 != pthread_mutex_unlock(mutex))
        goto error;

    return NULL;

error:
    return (void *)1;
} /* end check_max_nthreads_helper */


/*-------------------------------------------------------------------------
 * Function:    main
 *
 * Purpose:     Determines the maximum number of threads that can be run
 *              concurrently, up to MAX_THREADS_USABLE.
 *
 * Return:      Success: 0
 *              Failure: 1
 *
 * Programmer:  Neil Fortner
 *              March 18, 2013
 *
 *-------------------------------------------------------------------------
 */
int
main(int argc, char **argv)
{
    pthread_t *thread = NULL;
    pthread_attr_t threadattr;
    pthread_mutex_t mutex;
    _Bool is_threadattr_init = FALSE;
    _Bool is_mutex_init = FALSE;
    FILE *outfile = NULL;
    void *join_ret;
    int failed = FALSE;
    int num_threads = 1;
    int i;

    /* Print message about test */
    puts("Checking how many threads can be run at once...");
    fflush(stdout);

    /* Initialize thread attribute */
    if(0 != pthread_attr_init(&threadattr))
        TEST_ERROR;
    is_threadattr_init = TRUE;

    /* Set threads to joinable */
    if(0 != pthread_attr_setdetachstate(&threadattr, PTHREAD_CREATE_JOINABLE))
        TEST_ERROR;

    /* Initialize mutex */
    if(0 != pthread_mutex_init(&mutex, NULL))
        TEST_ERROR;
    is_mutex_init = TRUE;

    /* Allocate array of threads */
    if(NULL == (thread = (pthread_t *)malloc(MAX_THREADS_USABLE * sizeof(pthread_t))))
        TEST_ERROR;

    /* Lock mutex so created threads do */
    if(0 != pthread_mutex_lock(&mutex))
        TEST_ERROR;

    /* Try to launch MAX_THREADS_USABLE threads.  num_threads starts at 1
     * because this thread has already been created. */
    for(; num_threads < MAX_THREADS_USABLE; num_threads++)
        if(0 != pthread_create(&(thread[num_threads - 1]), &threadattr, check_max_nthreads_helper, &mutex))
            break;

    /* Unlock mutex */
    if(0 != pthread_mutex_unlock(&mutex))
        failed = TRUE;

    /* Join threads */
    for(i = 0; i < (num_threads - 1); i++) {
        if(0 != pthread_join(thread[i], &join_ret))
            failed = TRUE;
        else
            if(join_ret)
                failed = TRUE;
    } /* end for */

    /* Close */
    free(thread);
    thread = NULL;
    if(0 != pthread_mutex_destroy(&mutex))
        failed = TRUE;
    is_mutex_init = FALSE;
    if(0 != pthread_attr_destroy(&threadattr))
        failed = TRUE;
    is_threadattr_init = FALSE;

    /* Check for non-interrupting failure */
    if(failed)
        TEST_ERROR;

    /* If we were unable to spawn a single thread, assume somethign went wrong
     * (this library would not be very useful in this case at any rate) */
    if(num_threads == 1)
        TEST_ERROR;

    /* Create output file to store result */
    if(NULL == (outfile = fopen(MAX_NTHREADS_FILENAME, "w")))
        TEST_ERROR;

    /* Write result to output file */
    fprintf(outfile, "%d\n", num_threads);

    /* Close output file */
    if(0 != fclose(outfile))
        TEST_ERROR;

    /* Print message Indicating */
    printf("Result: %d\n", num_threads);
    fflush(stdout);

    return 0;

error:
    if(thread)
        free(thread);
    if(is_mutex_init)
        (void)pthread_mutex_destroy(&mutex);
    if(is_threadattr_init)
        (void)pthread_attr_destroy(&threadattr);

    return 1;
} /* end main() */

