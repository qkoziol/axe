/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include "axe_test.h"


/*
 * Macros
 */
#ifdef TEST_EXPRESS
#define SIMPLE_NITER 10
#define NECESSARY_NITER 10
#define SUFFICIENT_NITER 10
#define BARRIER_NITER 10
#define GET_OP_DATA_NITER 10
#define FINISH_ALL_NITER 10
#define FREE_OP_DATA_NITER 10
#define REMOVE_NITER 20
#define REMOVE_ALL_NITER 20
#define TERMINATE_ENGINE_NITER 20
#define NUM_THREADS_NITER 50
#define FRACTAL_NITER 10
#define FRACTAL_NCHILDREN 2
#define FRACTAL_NTASKS 1000
#define FRACTAL_NODEP_NITER 10
#define FRACTAL_NODEP_NCHILDREN 2
#define FRACTAL_NODEP_NTASKS 1000
#define PARALLEL_NITER 5000
#define PARALLEL_NUM_THREADS_META 20
#define CREATE_REMOVE_NITER 10
#define CREATE_REMOVE_NTASKS 50
#define CREATE_REMOVE_ALL_NITER 10
#define CREATE_REMOVE_ALL_NTASKS 20
#define PILEUP_NITER 6
#define PILEUP_NTASKS 100
#else
#define SIMPLE_NITER 1000
#define NECESSARY_NITER 1000
#define SUFFICIENT_NITER 1000
#define BARRIER_NITER 1000
#define GET_OP_DATA_NITER 1000
#define FINISH_ALL_NITER 1000
#define FREE_OP_DATA_NITER 1000
#define REMOVE_NITER 2000
#define REMOVE_ALL_NITER 2000
#define TERMINATE_ENGINE_NITER 2000
#define NUM_THREADS_NITER 5000
#define FRACTAL_NITER 200
#define FRACTAL_NCHILDREN 2
#define FRACTAL_NTASKS 10000
#define FRACTAL_NODEP_NITER 200
#define FRACTAL_NODEP_NCHILDREN 2
#define FRACTAL_NODEP_NTASKS 10000
#define PARALLEL_NITER 5000
#define PARALLEL_NUM_THREADS_META 20
#define CREATE_REMOVE_NITER 500
#define CREATE_REMOVE_NTASKS 1000
#define CREATE_REMOVE_ALL_NITER 200
#define CREATE_REMOVE_ALL_NTASKS 200
#define PILEUP_NITER 100
#define PILEUP_NTASKS 10000
#endif


/*
 * Typedefs
 */
/* op_data struct for all tests */
typedef struct {
    AXE_engine_t engine;        /* The engine to run the test in (if the test does not create its own */
    size_t num_threads;         /* The number of threads in engine */
    OPA_int_t nfailed;          /* (Out) the number of failures of the test */
    OPA_int_t ncomplete;        /* (Out) the number of successful completions of the test */
    pthread_mutex_t *parallel_mutex; /* Mutex for exclusive access to tests that require a minimum number of threads, if running in parallel (test_parallel()) */
} test_helper_t;

/* Shared data for basic_task_t */
typedef struct {
    int max_ncalls;             /* Maximum number of calls to basic_task_worker() */
    OPA_int_t ncalls;           /* (Out) number of calls to basic_task_worker() */
} basic_task_shared_t;

/* op_data for basic_task_worker() */
typedef struct {
    basic_task_shared_t *shared; /* Shared task op_data */
    int failed;                 /* (Out) whether this task failed */
    int run_order;              /* (Out) order in which this task was run */
    size_t num_necessary_parents; /* (Out) num_necessary_parents parameter provided to task */
    size_t num_sufficient_parents; /* (Out) num_sufficient_parents parameter provided to task */
    pthread_mutex_t *mutex;     /* Mutex used for synchronization */
    pthread_cond_t *cond;       /* Condition variable for signaling main test thread */
    pthread_mutex_t *cond_mutex; /* Mutex associated with cond */
    int cond_signal_sent;       /* (Out) Whether the condition signal was sent */
} basic_task_t;


/*
 * Variables
 */
/* Perform each test once for each element in this array, each time with the
 * number of threads indicated by the element in this array */
size_t num_threads_g[] = {1, 2, 3, 5, 10};

/* For the number of threads corresponding to the same location in
 * num_threads_g, reduce the number of iterations by the factor idicated in this
 * array */
size_t iter_reduction_g[] = {1, 1, 1, 3, 5};

/* Definitions needed for limiting the maximum number of threads */
MAX_NTHREADS_DECL;


/*-------------------------------------------------------------------------
 * Function:    basic_task_worker
 *
 * Purpose:     Task worker for most tests.  First sends a condition
 *              signal if requested, then waits on a mutex if requested,
 *              then records num_necessary_parents,
 *              num_sufficient_parents, and the call order before
 *              returning.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
void
basic_task_worker(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_task_data)
{
    basic_task_t *task_data = (basic_task_t *)_task_data;
    size_t i;

    assert(task_data);
    assert(task_data->shared);

    /* Send the condition signal, if requested */
    if(task_data->cond) {
        assert(task_data->cond_mutex);
        if(0 != pthread_mutex_lock(task_data->cond_mutex))
            task_data->failed = 1;
        if(0 != pthread_cond_signal(task_data->cond))
            task_data->failed = 1;
        task_data->cond_signal_sent = 1;
        if(0 != pthread_mutex_unlock(task_data->cond_mutex))
            task_data->failed = 1;
    } /* end if */

    /* Lock and unlock the mutex, if provided, to prevent exectuion until we are
     * allowed */
    if(task_data->mutex) {
        if(0 != pthread_mutex_lock(task_data->mutex))
            task_data->failed = 1;
        if(0 != pthread_mutex_unlock(task_data->mutex))
            task_data->failed = 1;
    } /* end if */

    /* Pass num_necessary_parents and num_sufficient_parents to caller */
    task_data->num_necessary_parents = num_necessary_parents;
    task_data->num_sufficient_parents = num_sufficient_parents;

    /* Make sure this task hasn't been called yet */
    if(task_data->run_order >= 0)
        task_data->failed = 1;

    /* Retrieve and increment number of calls to shared task struct, this is the
     * call order for this task */
    task_data->run_order = OPA_fetch_and_incr_int(&task_data->shared->ncalls);

    /* Make sure we are not going past the expected number of calls */
    if(task_data->run_order >= task_data->shared->max_ncalls)
        task_data->failed = 1;

    /* Decrement ref counts on parent arrays, as required */
    for(i = 0; i < num_necessary_parents; i++)
        if(AXEfinish(engine, necessary_parents[i]) != AXE_SUCCEED)
            task_data->failed = 1;
    for(i = 0; i < num_sufficient_parents; i++)
        if(AXEfinish(engine, sufficient_parents[i]) != AXE_SUCCEED)
            task_data->failed = 1;

    return;
} /* end basic_task_worker() */


/*-------------------------------------------------------------------------
 * Function:    basic_task_free
 *
 * Purpose:     Frees a dynamically allocated basic_task_t and
 *              basic_task_shared_t.  Always frees the shared struct, so
 *              if there is more than one top-level struct you will have
 *              to modify the shared struct and this function to use
 *              reference counting.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
void
basic_task_free(void *_task_data)
{
    basic_task_t *task_data = (basic_task_t *)_task_data;

    assert(task_data);
    assert(task_data->shared);

    free(task_data->shared);
    if(task_data->mutex) {
        /* Need to lock and unlock mutex before freeing to make sure other
         * threads are done with it */
        (void)pthread_mutex_lock(task_data->mutex);
        (void)pthread_mutex_unlock(task_data->mutex);
        (void)pthread_mutex_destroy(task_data->mutex);
        free(task_data->mutex);
    } /* end if */
    if(task_data->cond) {
        (void)pthread_cond_destroy(task_data->cond);
        free(task_data->cond);
    } /* end if */
    if(task_data->cond_mutex) {
        (void)pthread_mutex_destroy(task_data->cond_mutex);
        free(task_data->cond_mutex);
    } /* end if */
    free(task_data);
} /* end basic_task_free() */


/*-------------------------------------------------------------------------
 * Function:    test_simple_helper
 *
 * Purpose:     Tests basic task creation functionality of the AXE
 *              library.  Does not use dependencies.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
void
test_simple_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_task_t task[3];
    AXE_status_t status;
    basic_task_t task_data[3];
    basic_task_t *dyn_task_data;
    basic_task_shared_t shared_task_data;
    basic_task_shared_t *dyn_shared_task_data;
    int i;

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
        task_data[i].cond_signal_sent = 0;
    } /* end for */


    /*
     * Test 1: Single task
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 1;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create simple task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for task to complete */
    if(AXEwait(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 1; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 1)
        TEST_ERROR;

    /* Close task */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /*
     * Test 2: Three tasks
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 3;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create tasks */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[1], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(helper_data->engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[2], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(helper_data->engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    for(i = 0; i < 3; i++) {
        if(task_data[i].run_order == -1)
            TEST_ERROR;
        if(task_data[i].num_necessary_parents != 0)
            TEST_ERROR;
        if(task_data[i].num_sufficient_parents != 0)
            TEST_ERROR;
    } /* end for */
    for(i = 3; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 3)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 3: No task id requested
     */
    /* Note that we no longer support creation of tasks without task ids.  Keep
     * this test around anyways and just immediately close ids.  -NAF 4/17/2013
     */
    /* Use dynamic allocation so we do not run into problems if this function
     * returns while a task is still running, which would otherwise cause its
     * task data to go out of scope */
    /* Allocate and initialize shared task data struct */
    if(NULL == (dyn_shared_task_data = (basic_task_shared_t *)malloc(sizeof(basic_task_shared_t))))
        TEST_ERROR;
    dyn_shared_task_data->max_ncalls = 1;
    OPA_store_int(&dyn_shared_task_data->ncalls, 0);

    /* Allocate and initialize task data struct */
    if(NULL == (dyn_task_data = (basic_task_t *)malloc(sizeof(basic_task_t))))
        TEST_ERROR;
    dyn_task_data->shared = dyn_shared_task_data;
    dyn_task_data->failed = 0;
    dyn_task_data->run_order = -1;
    if(NULL == (dyn_task_data->mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t))))
        TEST_ERROR;
    if(0 != pthread_mutex_init(dyn_task_data->mutex, NULL))
        TEST_ERROR;
    if(NULL == (dyn_task_data->cond = (pthread_cond_t *)malloc(sizeof(pthread_cond_t))))
        TEST_ERROR;
    if(0 != pthread_cond_init(dyn_task_data->cond, NULL))
        TEST_ERROR;
    if(NULL == (dyn_task_data->cond_mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t))))
        TEST_ERROR;
    if(0 != pthread_mutex_init(dyn_task_data->cond_mutex, NULL))
        TEST_ERROR;
    dyn_task_data->cond_signal_sent = 0;

    /* Lock mutex before launching task so it does not complete and free
     * dyn_task_data before we can check it */
    if(0 != pthread_mutex_lock(dyn_task_data->mutex))
        TEST_ERROR;

    /* Create simple task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            dyn_task_data, basic_task_free) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for condition signal from thread, to guarantee that it actually ran
     */
    if(0 != pthread_mutex_lock(dyn_task_data->cond_mutex))
        TEST_ERROR;
    if(dyn_task_data->cond_signal_sent != 1)
        if(0 != pthread_cond_wait(dyn_task_data->cond, dyn_task_data->cond_mutex))
            TEST_ERROR;
    if(dyn_task_data->cond_signal_sent != 1)
        TEST_ERROR;
    if(0 != pthread_mutex_unlock(dyn_task_data->cond_mutex))
        TEST_ERROR;

    /* Unlock mutex */
    if(0 != pthread_mutex_unlock(dyn_task_data->mutex))
        TEST_ERROR;


    /*
     * Test 4: No worker task
     */
    /* Create simple task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, NULL, NULL, NULL)
            != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for task to complete */
    if(AXEwait(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;

    /* Close task */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 5: No task id requested and no worker task
     */
    /* See note for test 3  -NAF 4/17/2013*/
    /* Create simple task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, NULL, NULL, NULL)
            != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Close
     */
    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_simple_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_necessary_helper
 *
 * Purpose:     Tests functionality of necessary task dependencies.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
void
test_necessary_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_task_t task[10];
    AXE_task_t parent_task[10];
    AXE_status_t status;
    basic_task_t task_data[10];
    basic_task_shared_t shared_task_data;
    pthread_mutex_t mutex1, mutex2;
    int i;

    /* Initialize mutexes */
    if(0 != pthread_mutex_init(&mutex1, NULL))
        TEST_ERROR;
    if(0 != pthread_mutex_init(&mutex2, NULL))
        TEST_ERROR;

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
        task_data[i].cond_signal_sent = 0;
    } /* end for */


    /*
     * Test 1: Two task chain
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 2;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create first task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second task */
    if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[1].run_order != 1)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 2; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 2)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 2: One parent, two children
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 3;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create parent task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create first child task */
    if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second child task */
    if(AXEgenerate_task_id(helper_data->engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[2], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(helper_data->engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if((task_data[1].run_order < 1) || (task_data[1].run_order > 2))
        TEST_ERROR;
    if((task_data[2].run_order < 1) || (task_data[2].run_order > 2))
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[2].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[2].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 3; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 3)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 3: Two parents, one child
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 3;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create first parent task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second parent task */
    if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[1], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child task */
    parent_task[0] = task[0];
    parent_task[1] = task[1];
    if(AXEgenerate_task_id(helper_data->engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[2], 2, parent_task, 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(helper_data->engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if((task_data[0].run_order < 0) || (task_data[0].run_order > 1))
        TEST_ERROR;
    if((task_data[1].run_order < 0) || (task_data[1].run_order > 1))
        TEST_ERROR;
    if(task_data[2].run_order != 2)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[2].num_necessary_parents != 2)
        TEST_ERROR;
    if(task_data[2].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 3; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 3)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 4: Three parents, one child, ordering tested with mutexes
     */
    /* Only test with at least 3 worker threads, otherwise it could deadlock
     * because this test assumes parallel execution */
    if(helper_data->num_threads >= 3) {
        /* If running in parallel, make sure this does not run concurrently with
         * any other tests that require a certain number of threads (in the
         * shared engine) */
        if(helper_data->parallel_mutex)
            if(0 != pthread_mutex_lock(helper_data->parallel_mutex))
                TEST_ERROR;

        /* Initialize shared task data struct */
        shared_task_data.max_ncalls = 4;
        OPA_store_int(&shared_task_data.ncalls, 0);

        /* Initialize task data struct */
        for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
            task_data[i].run_order = -1;
        task_data[0].mutex = &mutex1;
        task_data[1].mutex = &mutex2;

        /* Lock mutexes */
        if(0 != pthread_mutex_lock(task_data[0].mutex))
            TEST_ERROR;
        if(0 != pthread_mutex_lock(task_data[1].mutex))
            TEST_ERROR;

        /* Create first parent task */
        if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
                &task_data[0], NULL) != AXE_SUCCEED)
            TEST_ERROR;

        /* Create second parent task */
        if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(helper_data->engine, task[1], 0, NULL, 0, NULL, basic_task_worker,
                &task_data[1], NULL) != AXE_SUCCEED)
            TEST_ERROR;

        /* Create third parent task */
        if(AXEgenerate_task_id(helper_data->engine, &task[2]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(helper_data->engine, task[2], 0, NULL, 0, NULL, basic_task_worker,
                &task_data[2], NULL) != AXE_SUCCEED)
            TEST_ERROR;

        /* Create child task */
        parent_task[0] = task[0];
        parent_task[1] = task[1];
        parent_task[2] = task[2];
        if(AXEgenerate_task_id(helper_data->engine, &task[3]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(helper_data->engine, task[3], 3, parent_task, 0, NULL, basic_task_worker,
                &task_data[3], NULL) != AXE_SUCCEED)
            TEST_ERROR;

        /* Wait for third parent task to complete */
        if(AXEwait(helper_data->engine, task[2]) != AXE_SUCCEED)
            TEST_ERROR;

        /* Make sure the blocked parent tasks have not yet completed, and the
         * child has not been scheduled */
        if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[2], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[3], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;

        /* Release first mutex */
        if(0 != pthread_mutex_unlock(task_data[0].mutex))
            TEST_ERROR;

        /* Wait for first parent task to complete */
        if(AXEwait(helper_data->engine, task[0]) != AXE_SUCCEED)
            TEST_ERROR;

        /* Make sure the blocked parent task has not run yet, and the child has
         * not been scheduled */
        if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[2], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[3], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;

        /* Release second mutex */
        if(0 != pthread_mutex_unlock(task_data[1].mutex))
            TEST_ERROR;

        /* Wait for child task to complete */
        if(AXEwait(helper_data->engine, task[3]) != AXE_SUCCEED)
            TEST_ERROR;

        /* Verify results */
        if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[2], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[3], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
            if(task_data[i].failed > 0)
                TEST_ERROR;
        if(task_data[0].run_order != 1)
            TEST_ERROR;
        if(task_data[1].run_order != 2)
            TEST_ERROR;
        if(task_data[2].run_order != 0)
            TEST_ERROR;
        if(task_data[3].run_order != 3)
            TEST_ERROR;
        if(task_data[0].num_necessary_parents != 0)
            TEST_ERROR;
        if(task_data[0].num_sufficient_parents != 0)
            TEST_ERROR;
        if(task_data[1].num_necessary_parents != 0)
            TEST_ERROR;
        if(task_data[1].num_sufficient_parents != 0)
            TEST_ERROR;
        if(task_data[2].num_necessary_parents != 0)
            TEST_ERROR;
        if(task_data[2].num_sufficient_parents != 0)
            TEST_ERROR;
        if(task_data[3].num_necessary_parents != 3)
            TEST_ERROR;
        if(task_data[3].num_sufficient_parents != 0)
            TEST_ERROR;
        for(i = 4; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
            if(task_data[i].run_order != -1)
                TEST_ERROR;
        if(OPA_load_int(&shared_task_data.ncalls) != 4)
            TEST_ERROR;

        /* Close tasks */
        if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(helper_data->engine, task[1]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(helper_data->engine, task[2]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(helper_data->engine, task[3]) != AXE_SUCCEED)
            TEST_ERROR;
        for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
            task_data[i].mutex = NULL;

        /* Unlock parallel mutex */
        if(helper_data->parallel_mutex)
            if(0 != pthread_mutex_unlock(helper_data->parallel_mutex))
                TEST_ERROR;
    } /* end if */


    /*
     * Test 5: Nine parents, one child
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 10;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;
    task_data[0].mutex = &mutex1;

    /* Lock mutex */
    if(0 != pthread_mutex_lock(task_data[0].mutex))
        TEST_ERROR;

    /* Create first parent task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create secondary parent tasks */
    for(i = 1; i <= 8; i++) {
        if(AXEgenerate_task_id(helper_data->engine, &task[i]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(helper_data->engine, task[i], 1, &task[0], 0, NULL, basic_task_worker,
                &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;
    } /* end for */

    /* Create child task */
    if(AXEgenerate_task_id(helper_data->engine, &task[9]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[9], 9, task, 0, NULL, basic_task_worker,
            &task_data[9], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Make sure the primary parent task has not yet completed, and the other
     * tasks have not been scheduled */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 1; i <= 9; i++) {
        if(AXEget_status(helper_data->engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;
    } /* end for */

    /* Release mutex */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(helper_data->engine, task[9]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i < 10; i++) {
        if(AXEget_status(helper_data->engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
    } /* end for */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 1; i <= 8; i++) {
        if(task_data[i].run_order == -1)
            TEST_ERROR;
        if(task_data[i].num_necessary_parents != 1)
            TEST_ERROR;
        if(task_data[i].num_sufficient_parents != 0)
            TEST_ERROR;
    } /* end for */
    if(task_data[9].run_order != 9)
        TEST_ERROR;
    if(task_data[9].num_necessary_parents != 9)
        TEST_ERROR;
    if(task_data[9].num_sufficient_parents != 0)
        TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 10)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i < 10; i++)
        if(AXEfinish(helper_data->engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].mutex = NULL;


    /*
     * Test 6: One parent, nine children
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 10;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;
    task_data[0].mutex = &mutex1;

    /* Lock mutex */
    if(0 != pthread_mutex_lock(task_data[0].mutex))
        TEST_ERROR;

    /* Create parent task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child tasks */
    for(i = 1; i <= 9; i++) {
        if(AXEgenerate_task_id(helper_data->engine, &task[i]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(helper_data->engine, task[i], 1, &task[0], 0, NULL, basic_task_worker,
                &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;
    } /* end for */

    /* Make sure the primary parent task has not yet completed, and the other
     * tasks have not been scheduled */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 1; i <= 9; i++) {
        if(AXEget_status(helper_data->engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;
    } /* end for */

    /* Release mutex */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete */
    for(i = 1; i <= 9; i++)
        if(AXEwait(helper_data->engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;

    /* Verify results */
    for(i = 0; i < 10; i++) {
        if(AXEget_status(helper_data->engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
    } /* end for */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 1; i <= 9; i++) {
        if(task_data[i].run_order == -1)
            TEST_ERROR;
        if(task_data[i].num_necessary_parents != 1)
            TEST_ERROR;
        if(task_data[i].num_sufficient_parents != 0)
            TEST_ERROR;
    } /* end for */
    if(OPA_load_int(&shared_task_data.ncalls) != 10)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i < 10; i++)
        if(AXEfinish(helper_data->engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].mutex = NULL;


    /*
     * Close
     */
    /* Destroy mutexes */
    if(0 != pthread_mutex_destroy(&mutex1))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&mutex2))
        TEST_ERROR;

    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    (void)pthread_mutex_destroy(&mutex1);
    (void)pthread_mutex_destroy(&mutex2);

    OPA_incr_int(&helper_data->nfailed);

    return;
    
} /* end test_necessary_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_sufficient_helper
 *
 * Purpose:     Tests functionality of sufficient task dependencies.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
void
test_sufficient_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_task_t task[10];
    AXE_task_t parent_task[10];
    AXE_status_t status;
    basic_task_t task_data[10];
    basic_task_shared_t shared_task_data;
    pthread_mutex_t mutex1, mutex2;
    int i;

    /* Initialize mutexes */
    if(0 != pthread_mutex_init(&mutex1, NULL))
        TEST_ERROR;
    if(0 != pthread_mutex_init(&mutex2, NULL))
        TEST_ERROR;

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
        task_data[i].cond_signal_sent = 0;
    } /* end for */


    /*
     * Test 1: Two task chain
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 2;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create first task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second task */
    if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[1], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[1].run_order != 1)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 1)
        TEST_ERROR;
    for(i = 2; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 2)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 2: One parent, two children
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 3;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create parent task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create first child task */
    if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[1], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second child task */
    if(AXEgenerate_task_id(helper_data->engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[2], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(helper_data->engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if((task_data[1].run_order < 1) || (task_data[1].run_order > 2))
        TEST_ERROR;
    if((task_data[2].run_order < 1) || (task_data[2].run_order > 2))
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 1)
        TEST_ERROR;
    if(task_data[2].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[2].num_sufficient_parents != 1)
        TEST_ERROR;
    for(i = 3; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 3)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 3: Two parents, one child
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 3;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create first parent task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second parent task */
    if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[1], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child task */
    parent_task[0] = task[0];
    parent_task[1] = task[1];
    if(AXEgenerate_task_id(helper_data->engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[2], 0, NULL, 2, parent_task, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(helper_data->engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[2].run_order == 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[2].num_necessary_parents != 0)
        TEST_ERROR;
    if((task_data[2].num_sufficient_parents < 1)
            || (task_data[2].num_sufficient_parents > 2))
        TEST_ERROR;
    for(i = 3; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 3)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 4: Two parents, one child, ordering tested with mutexes
     */
    /* Only test with at least 2 worker threads, otherwise it could deadlock
     * because this test assumes parallel execution */
    if(helper_data->num_threads >= 2) {
        /* If running in parallel, make sure this does not run concurrently with
         * any other tests that require a certain number of threads (in the
         * shared engine) */
        if(helper_data->parallel_mutex)
            if(0 != pthread_mutex_lock(helper_data->parallel_mutex))
                TEST_ERROR;

        /* Initialize shared task data struct */
        shared_task_data.max_ncalls = 3;
        OPA_store_int(&shared_task_data.ncalls, 0);

        /* Initialize task data struct */
        for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
            task_data[i].run_order = -1;
        task_data[0].mutex = &mutex1;
        task_data[1].mutex = &mutex2;

        /* Lock mutexes */
        if(0 != pthread_mutex_lock(task_data[0].mutex))
            TEST_ERROR;
        if(0 != pthread_mutex_lock(task_data[1].mutex))
            TEST_ERROR;

        /* Create first parent task */
        if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
                &task_data[0], NULL) != AXE_SUCCEED)
            TEST_ERROR;

        /* Create second parent task */
        if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(helper_data->engine, task[1], 0, NULL, 0, NULL, basic_task_worker,
                &task_data[1], NULL) != AXE_SUCCEED)
            TEST_ERROR;

        /* Create child task */
        parent_task[0] = task[0];
        parent_task[1] = task[1];
        if(AXEgenerate_task_id(helper_data->engine, &task[2]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(helper_data->engine, task[2], 0, NULL, 2, parent_task, basic_task_worker,
                &task_data[2], NULL) != AXE_SUCCEED)
            TEST_ERROR;

        /* Make sure the parent tasks have not finished, and child has not been
         * scheduled */
        if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[2], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;

        /* Release first mutex */
        if(0 != pthread_mutex_unlock(task_data[0].mutex))
            TEST_ERROR;

        /* Wait for child task to complete */
        if(AXEwait(helper_data->engine, task[2]) != AXE_SUCCEED)
            TEST_ERROR;

        /* Make sure the first parent and child have completed, and the second
         * parent has not finished */
        if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[2], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;

        /* Release second mutex */
        if(0 != pthread_mutex_unlock(task_data[1].mutex))
            TEST_ERROR;

        /* Wait for second parent task to complete */
        if(AXEwait(helper_data->engine, task[1]) != AXE_SUCCEED)
            TEST_ERROR;

        /* Verify results */
        if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[2], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
            if(task_data[i].failed > 0)
                TEST_ERROR;
        if(task_data[0].run_order != 0)
            TEST_ERROR;
        if(task_data[1].run_order != 2)
            TEST_ERROR;
        if(task_data[2].run_order != 1)
            TEST_ERROR;
        if(task_data[0].num_necessary_parents != 0)
            TEST_ERROR;
        if(task_data[0].num_sufficient_parents != 0)
            TEST_ERROR;
        if(task_data[1].num_necessary_parents != 0)
            TEST_ERROR;
        if(task_data[1].num_sufficient_parents != 0)
            TEST_ERROR;
        if(task_data[2].num_necessary_parents != 0)
            TEST_ERROR;
        if(task_data[2].num_sufficient_parents != 1)
            TEST_ERROR;
        for(i = 3; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
            if(task_data[i].run_order != -1)
                TEST_ERROR;
        if(OPA_load_int(&shared_task_data.ncalls) != 3)
            TEST_ERROR;

        /* Close tasks */
        if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(helper_data->engine, task[1]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(helper_data->engine, task[2]) != AXE_SUCCEED)
            TEST_ERROR;
        for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
            task_data[i].mutex = NULL;

        /* Unlock parallel mutex */
        if(helper_data->parallel_mutex)
            if(0 != pthread_mutex_unlock(helper_data->parallel_mutex))
                TEST_ERROR;
    } /* end if */


    /*
     * Test 5: Nine parents, one child
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 10;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;
    task_data[0].mutex = &mutex1;

    /* Lock mutex */
    if(0 != pthread_mutex_lock(task_data[0].mutex))
        TEST_ERROR;

    /* Create first parent task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create secondary parent tasks */
    for(i = 1; i <= 8; i++) {
        if(AXEgenerate_task_id(helper_data->engine, &task[i]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(helper_data->engine, task[i], 0, NULL, 1, &task[0], basic_task_worker,
                &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;
    } /* end for */

    /* Create child task */
    if(AXEgenerate_task_id(helper_data->engine, &task[9]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[9], 0, NULL, 9, task, basic_task_worker,
            &task_data[9], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Make sure the primary parent task has not yet completed, and the other
     * tasks have not been scheduled */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 1; i <= 9; i++) {
        if(AXEget_status(helper_data->engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;
    } /* end for */

    /* Release mutex */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete */
    for(i = 1; i <= 9; i++)
        if(AXEwait(helper_data->engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;

    /* Verify results */
    for(i = 0; i < 10; i++) {
        if(AXEget_status(helper_data->engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
    } /* end for */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 1; i <= 9; i++)
        if(task_data[i].run_order == -1)
            TEST_ERROR;
    for(i = 1; i <= 8; i++) {
        if(task_data[i].num_necessary_parents != 0)
            TEST_ERROR;
        if(task_data[i].num_sufficient_parents != 1)
            TEST_ERROR;
    } /* end for */
    if(task_data[9].num_necessary_parents != 0)
        TEST_ERROR;
    if((task_data[9].num_sufficient_parents < 1)
            || (task_data[9].num_sufficient_parents > 9))
        TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 10)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i < 10; i++)
        if(AXEfinish(helper_data->engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].mutex = NULL;


    /*
     * Test 6: One parent, nine children
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 10;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;
    task_data[0].mutex = &mutex1;

    /* Lock mutex */
    if(0 != pthread_mutex_lock(task_data[0].mutex))
        TEST_ERROR;

    /* Create parent task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child tasks */
    for(i = 1; i <= 9; i++) {
        if(AXEgenerate_task_id(helper_data->engine, &task[i]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(helper_data->engine, task[i], 0, NULL, 1, &task[0], basic_task_worker,
                &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;
    } /* end for */

    /* Make sure the primary parent task has not yet completed, and the other
     * tasks have not been scheduled */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 1; i <= 9; i++) {
        if(AXEget_status(helper_data->engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;
    } /* end for */

    /* Release mutex */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete */
    for(i = 1; i <= 9; i++)
        if(AXEwait(helper_data->engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;

    /* Verify results */
    for(i = 0; i < 10; i++) {
        if(AXEget_status(helper_data->engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
    } /* end for */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 1; i <= 9; i++) {
        if(task_data[i].run_order == -1)
            TEST_ERROR;
        if(task_data[i].num_necessary_parents != 0)
            TEST_ERROR;
        if(task_data[i].num_sufficient_parents != 1)
            TEST_ERROR;
    } /* end for */
    if(OPA_load_int(&shared_task_data.ncalls) != 10)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i < 10; i++)
        if(AXEfinish(helper_data->engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].mutex = NULL;


    /*
     * Test 7: Sufficient and necessary parents of same task
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 3;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create first parent task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second parent task */
    if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[1], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child task */
    if(AXEgenerate_task_id(helper_data->engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[2], 1, &task[0], 1, &task[1],
            basic_task_worker, &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(helper_data->engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if((task_data[0].run_order < 0) || (task_data[0].run_order > 1))
        TEST_ERROR;
    if((task_data[1].run_order < 0) || (task_data[1].run_order > 1))
        TEST_ERROR;
    if(task_data[2].run_order != 2)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[2].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[2].num_sufficient_parents != 1)
        TEST_ERROR;
    for(i = 3; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 3)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Close
     */
    /* Destroy mutexes */
    if(0 != pthread_mutex_destroy(&mutex1))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&mutex2))
        TEST_ERROR;

    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    (void)pthread_mutex_destroy(&mutex1);
    (void)pthread_mutex_destroy(&mutex2);

    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_sufficient_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_barrier_helper
 *
 * Purpose:     Tests creation of barrier tasks.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
void
test_barrier_helper(AXE_engine_t _engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_engine_t engine;
    AXE_engine_attr_t engine_attr;
    AXE_task_t task[11];
    AXE_task_t parent_task[2];
    AXE_status_t status;
    basic_task_t task_data[11];
    basic_task_shared_t shared_task_data;
    pthread_mutex_t mutex1;
    int i;

    /* Initialize mutex */
    if(0 != pthread_mutex_init(&mutex1, NULL))
        TEST_ERROR;

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
        task_data[i].cond_signal_sent = 0;
    } /* end for */

    /* Reserve threads for engine */
    MAX_NTHREADS_RESERVE(helper_data->num_threads, TEST_ERROR);

    /* Initialize engine attribute */
    if(AXEengine_attr_init(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create AXE engine */
    if(AXEset_num_threads(&engine_attr, helper_data->num_threads) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_engine(&engine, &engine_attr) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 1: Single barrier task
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 1;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create barrier task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_barrier_task(engine, task[0], basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 1; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 1)
        TEST_ERROR;

    /* Close task */
    if(AXEfinish(engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 2: Barrier task with one parent
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 2;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create first task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create barrier task */
    if(AXEgenerate_task_id(engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_barrier_task(engine, task[1], basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[1].run_order != 1)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_necessary_parents > 1)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 2; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 2)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 3: Barrier task with one parent and one child
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 3;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create first task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create barrier task */
    if(AXEgenerate_task_id(engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_barrier_task(engine, task[1], basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child task */
    if(AXEgenerate_task_id(engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[2], 1, &task[1], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(engine, task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[1].run_order != 1)
        TEST_ERROR;
    if(task_data[2].run_order != 2)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_necessary_parents > 1)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[2].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[2].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 3; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 3)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 4: One parent, two children, barrier
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 4;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create parent task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child tasks */
    if(AXEgenerate_task_id(engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[2], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create barrier task */
    if(AXEgenerate_task_id(engine, &task[3]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_barrier_task(engine, task[3], basic_task_worker,
            &task_data[3], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(engine, task[3]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i <= 3; i++) {
        if(AXEget_status(engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
    } /* end for */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if((task_data[1].run_order < 1) || (task_data[1].run_order > 2))
        TEST_ERROR;
    if((task_data[2].run_order < 1) || (task_data[2].run_order > 2))
        TEST_ERROR;
    if(task_data[3].run_order != 3)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[2].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[2].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[3].num_necessary_parents > 2)
        TEST_ERROR;
    if(task_data[3].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 4; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 4)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i <= 3; i++)
        if(AXEfinish(engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;


    /*
     * Test 5: One parent, two children, barrier, parent held by mutex
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 4;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;
    task_data[0].mutex = &mutex1;

    /* Lock mutex */
    if(0 != pthread_mutex_lock(task_data[0].mutex))
        TEST_ERROR;

    /* Create parent task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child tasks */
    if(AXEgenerate_task_id(engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[2], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create barrier task */
    if(AXEgenerate_task_id(engine, &task[3]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_barrier_task(engine, task[3], basic_task_worker, &task_data[3],
            NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Make sure the parent task has not finished, and other tasks have not been
     * scheduled */
    if(AXEget_status(engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 0; i <= 3; i++) {
        if(AXEget_status(engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
    } /* end for */

    /* Release mutex */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(engine, task[3]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i <= 3; i++) {
        if(AXEget_status(engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
    } /* end for */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if((task_data[1].run_order < 1) || (task_data[1].run_order > 2))
        TEST_ERROR;
    if((task_data[2].run_order < 1) || (task_data[2].run_order > 2))
        TEST_ERROR;
    if(task_data[3].run_order != 3)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[2].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[2].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[3].num_necessary_parents != 2)
        TEST_ERROR;
    if(task_data[3].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 4; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 4)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i <= 3; i++)
        if(AXEfinish(engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].mutex = NULL;


    /*
     * Test 6: Complex: One top-level parent, three second-level children,
     * sufficient child of top-level parent and one second-level child, two
     * barrier tasks at bottom.
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 7;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create parent task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second-level child tasks */
    for(i = 1; i <= 3; i++) {
        if(AXEgenerate_task_id(engine, &task[i]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(engine, task[i], 1, &task[0], 0, NULL,
                basic_task_worker, &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;
    } /* end for */

    /* Create sufficient child task */
    parent_task[0] = task[0];
    parent_task[1] = task[2];
    if(AXEgenerate_task_id(engine, &task[4]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[4], 0, NULL, 2, parent_task,
            basic_task_worker, &task_data[4], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create first barrier task */
    if(AXEgenerate_task_id(engine, &task[5]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_barrier_task(engine, task[5], basic_task_worker, &task_data[5],
            NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second barrier task */
    if(AXEgenerate_task_id(engine, &task[6]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_barrier_task(engine, task[6], basic_task_worker, &task_data[6],
            NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(engine, task[6]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i <= 6; i++) {
        if(AXEget_status(engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
    } /* end for */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if((task_data[1].run_order < 1) || (task_data[1].run_order > 4))
        TEST_ERROR;
    if((task_data[2].run_order < 1) || (task_data[2].run_order > 4))
        TEST_ERROR;
    if((task_data[3].run_order < 1) || (task_data[3].run_order > 4))
        TEST_ERROR;
    if((task_data[4].run_order < 1) || (task_data[4].run_order > 4))
        TEST_ERROR;
    if(task_data[5].run_order != 5)
        TEST_ERROR;
    if(task_data[6].run_order != 6)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[2].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[2].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[3].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[3].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[4].num_necessary_parents != 0)
        TEST_ERROR;
    if((task_data[4].num_sufficient_parents < 1)
            || (task_data[4].num_sufficient_parents > 2))
        TEST_ERROR;
    if(task_data[5].num_necessary_parents > 4)
        TEST_ERROR;
    if(task_data[5].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[6].num_necessary_parents > 1)
        TEST_ERROR;
    if(task_data[6].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 7; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 7)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i <= 6; i++)
        if(AXEfinish(engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;


    /*
     * Test 7: Same as test 6 with the parent held by a mutex
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 7;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;
    task_data[0].mutex = &mutex1;

    /* Lock mutex */
    if(0 != pthread_mutex_lock(task_data[0].mutex))
        TEST_ERROR;

    /* Create parent task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second-level child tasks */
    for(i = 1; i <= 3; i++) {
        if(AXEgenerate_task_id(engine, &task[i]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(engine, task[i], 1, &task[0], 0, NULL,
                basic_task_worker, &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;
    } /* end for */

    /* Create sufficient child task */
    parent_task[0] = task[0];
    parent_task[1] = task[2];
    if(AXEgenerate_task_id(engine, &task[4]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[4], 0, NULL, 2, parent_task,
            basic_task_worker, &task_data[4], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create first barrier task */
    if(AXEgenerate_task_id(engine, &task[5]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_barrier_task(engine, task[5], basic_task_worker, &task_data[5],
            NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second barrier task */
    if(AXEgenerate_task_id(engine, &task[6]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_barrier_task(engine, task[6], basic_task_worker, &task_data[6],
            NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Make sure the parent task has not finished, and other tasks have not been
     * scheduled */
    if(AXEget_status(engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 0; i <= 6; i++) {
        if(AXEget_status(engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
    } /* end for */

    /* Release mutex */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(engine, task[6]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i <= 6; i++) {
        if(AXEget_status(engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
    } /* end for */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if((task_data[1].run_order < 1) || (task_data[1].run_order > 4))
        TEST_ERROR;
    if((task_data[2].run_order < 1) || (task_data[2].run_order > 4))
        TEST_ERROR;
    if((task_data[3].run_order < 1) || (task_data[3].run_order > 4))
        TEST_ERROR;
    if((task_data[4].run_order < 1) || (task_data[4].run_order > 4))
        TEST_ERROR;
    if(task_data[5].run_order != 5)
        TEST_ERROR;
    if(task_data[6].run_order != 6)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[2].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[2].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[3].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[3].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[4].num_necessary_parents != 0)
        TEST_ERROR;
    if((task_data[4].num_sufficient_parents < 1)
            || (task_data[4].num_sufficient_parents > 2))
        TEST_ERROR;
    if(task_data[5].num_necessary_parents != 4)
        TEST_ERROR;
    if(task_data[5].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[6].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[6].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 7; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 7)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i <= 6; i++)
        if(AXEfinish(engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].mutex = NULL;


    /*
     * Test 8: Nine parents
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 11;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;
    task_data[0].mutex = &mutex1;

    /* Lock mutex */
    if(0 != pthread_mutex_lock(task_data[0].mutex))
        TEST_ERROR;

    /* Create first parent task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create secondary parent tasks */
    for(i = 1; i <= 9; i++) {
        if(AXEgenerate_task_id(engine, &task[i]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(engine, task[i], 1, &task[0], 0, NULL,
                basic_task_worker, &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;
    } /* end for */

    /* Create barrier task */
    if(AXEgenerate_task_id(engine, &task[10]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_barrier_task(engine, task[10], basic_task_worker,
            &task_data[10], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Make sure the primary parent task has not yet completed, and the other
     * tasks have not been scheduled */
    if(AXEget_status(engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 1; i <= 10; i++) {
        if(AXEget_status(engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;
    } /* end for */

    /* Release mutex */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(engine, task[10]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i < 11; i++) {
        if(AXEget_status(engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
    } /* end for */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 1; i <= 9; i++) {
        if(task_data[i].run_order == -1)
            TEST_ERROR;
        if(task_data[i].num_necessary_parents != 1)
            TEST_ERROR;
        if(task_data[i].num_sufficient_parents != 0)
            TEST_ERROR;
    } /* end for */
    if(task_data[10].run_order != 10)
        TEST_ERROR;
    if(task_data[10].num_necessary_parents != 9)
        TEST_ERROR;
    if(task_data[10].num_sufficient_parents != 0)
        TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 11)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i < 11; i++)
        if(AXEfinish(engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].mutex = NULL;


    /*
     * Close
     */
    /* Terminate engine */
    AXE_test_exclude_close_on(engine);
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Release threads used by engine */
    MAX_NTHREADS_RELEASE(helper_data->num_threads, TEST_ERROR);

    /* Destroy engine attribute */
    if(AXEengine_attr_destroy(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Destroy mutex */
    if(0 != pthread_mutex_destroy(&mutex1))
        TEST_ERROR;

    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    (void)AXEterminate_engine(engine, FALSE);
    MAX_NTHREADS_RELEASE(helper_data->num_threads, );

    (void)AXEengine_attr_destroy(&engine_attr);

    (void)pthread_mutex_destroy(&mutex1);

    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_barrier_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_get_op_data_helper
 *
 * Purpose:     Tests functionality of AXEget_op_data().
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
void
test_get_op_data_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_task_t task;
    AXE_status_t status;
    basic_task_t task_data;
    void *op_data;
    basic_task_shared_t shared_task_data;

    /* Initialize task data struct */
    task_data.shared = &shared_task_data;
    task_data.failed = 0;
    task_data.mutex = NULL;
    task_data.cond = NULL;
    task_data.cond_mutex = NULL;
    task_data.cond_signal_sent = 0;


    /*
     * Test 1: Single task
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 1;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    task_data.run_order = -1;

    /* Create barrier task */
    if(AXEgenerate_task_id(helper_data->engine, &task) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task, 0, NULL, 0, NULL, basic_task_worker,
            &task_data, NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Check that op_data returned is task_data */
    if(AXEget_op_data(helper_data->engine, task, &op_data) != AXE_SUCCEED)
        TEST_ERROR;
    if(op_data != (void *)&task_data)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(helper_data->engine, task) != AXE_SUCCEED)
        TEST_ERROR;

    /* Check that op_data returned is task_data */
    if(AXEget_op_data(helper_data->engine, task, &op_data) != AXE_SUCCEED)
        TEST_ERROR;
    if(op_data != (void *)&task_data)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task, &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(task_data.failed > 0)
        TEST_ERROR;
    if(task_data.run_order != 0)
        TEST_ERROR;
    if(task_data.num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data.num_sufficient_parents != 0)
        TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 1)
        TEST_ERROR;

    /* Close task */
    if(AXEfinish(helper_data->engine, task) != AXE_SUCCEED)
        TEST_ERROR;

    /*
     * Close
     */
    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_get_op_data_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_finish_all_helper
 *
 * Purpose:     Tests functionality of AXEfinish_all().
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
void
test_finish_all_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_task_t task[2];
    AXE_status_t status;
    basic_task_t task_data[2];
    basic_task_shared_t shared_task_data;
    int i;

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
        task_data[i].cond_signal_sent = 0;
    } /* end for */


    /*
     * Test 1: No tasks
     */
    if(AXEfinish_all(helper_data->engine, 0, NULL) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 2: Two tasks
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 2;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create tasks */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[1], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    for(i = 0; i < 2; i++) {
        if(task_data[i].run_order == -1)
            TEST_ERROR;
        if(task_data[i].num_necessary_parents != 0)
            TEST_ERROR;
        if(task_data[i].num_sufficient_parents != 0)
            TEST_ERROR;
    } /* end for */
    if(OPA_load_int(&shared_task_data.ncalls) != 2)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish_all(helper_data->engine, 2, task) != AXE_SUCCEED)
        TEST_ERROR;

    /* Rely on AXE_test_exclude_close_on() in calling function to make sure
     * AXEfinish_all() actually closed the tasks here */


    /*
     * Close
     */
    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_finish_all_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_free_op_data_helper
 *
 * Purpose:     Tests functionality of free_op_data callback provided to
 *              AXEcreate_task()/AXEcreate_barrier_task().
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
/* op_data struct for free_op_data_worker */
typedef struct free_op_data_t {
    OPA_int_t ncalls;           /* Number of calls to free_op_data_worker */
    OPA_int_t rc;               /* Number of references to this struct.  It will be freed when it drops to 0. */
    pthread_cond_t cond;        /* Condition variable for signaling main test thread */
    pthread_mutex_t cond_mutex; /* Mutex associated with cond */
    int failed;                 /* Whether free_op_data_worker() failed */
} free_op_data_t;


/* Function that actually frees the op data for free_op_data test.  We can't
 * just use a local variable in the launcher thread because that could go out of
 * scope before free_op_data_worker finishes */
int
free_op_data_decr_ref(free_op_data_t *task_data)
{
    if(OPA_decr_and_test_int(&task_data->rc)) {
        if(0 != pthread_cond_destroy(&task_data->cond))
            TEST_ERROR;
        if(0 != pthread_mutex_destroy(&task_data->cond_mutex))
            TEST_ERROR;
        free(task_data);
    } /* end if */

    return 0;

error:
    return 1;
} /* end free_op_data_decr_ref() */


/* "free_op_data" callback for free_op_data test.  Marks that it has been called
 * for the specified op_data, sends a signal, and calls free_op_data_decr_ref().
 */
void
free_op_data_worker(void *_task_data)
{
    free_op_data_t *task_data = (free_op_data_t *)_task_data;

    assert(task_data);

    /* Lock the condition mutex */
    if(0 != pthread_mutex_lock(&task_data->cond_mutex))
        task_data->failed = 1;
    OPA_incr_int(&task_data->ncalls);
    if(0 != pthread_cond_signal(&task_data->cond))
        task_data->failed = 1;
    if(0 != pthread_mutex_unlock(&task_data->cond_mutex))
        task_data->failed = 1;

    /* Release task_data */
    if(free_op_data_decr_ref(task_data) != 0)
        task_data->failed = 1;

    return;
} /* end free_op_data_worker() */


/* Main test helper function */
void
test_free_op_data_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_task_t task[3];
    free_op_data_t *task_data[3];
    int i;


    /*
     * Test 1: Single task
     */
    /* Allocate and initialize task_data[0] */
    if(NULL == (task_data[0] = (free_op_data_t *)malloc(sizeof(free_op_data_t))))
        TEST_ERROR;
    OPA_store_int(&(task_data[0])->ncalls, 0);
    OPA_store_int(&(task_data[0])->rc, 2);
    if(0 != pthread_cond_init(&(task_data[0])->cond, NULL))
        TEST_ERROR;
    if(0 != pthread_mutex_init(&(task_data[0])->cond_mutex, NULL))
        TEST_ERROR;
    task_data[0]->failed = 0;

    /* Create simple task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, NULL, task_data[0],
            free_op_data_worker) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for task to complete */
    if(AXEwait(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify free_op_data has been called the correct number of times */
    if(OPA_load_int(&(task_data[0])->ncalls) != 0)
        TEST_ERROR;

    /* Close task */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for condition signal so we know the free_op_data callback has been
     * called */
    if(0 != pthread_mutex_lock(&(task_data[0])->cond_mutex))
        TEST_ERROR;
    if(OPA_load_int(&(task_data[0])->ncalls) == 0)
        if(0 != pthread_cond_wait(&(task_data[0])->cond, &(task_data[0])->cond_mutex))
            TEST_ERROR;
    if(0 != pthread_mutex_unlock(&(task_data[0])->cond_mutex))
        TEST_ERROR;

    /* Verify free_op_data has been called the correct number of times */
    if(OPA_load_int(&(task_data[0])->ncalls) != 1)
        TEST_ERROR;

    /* Release task_data[0] */
    if(free_op_data_decr_ref(task_data[0]) != 0)
        TEST_ERROR;
    task_data[0] = NULL;


    /*
     * Test 2: Three tasks
     */
    /* Allocate and initialize task_data */
    for(i = 0; i <= 2; i++) {
        if(NULL == (task_data[i] = (free_op_data_t *)malloc(sizeof(free_op_data_t))))
            TEST_ERROR;
        OPA_store_int(&(task_data[i])->ncalls, 0);
        OPA_store_int(&(task_data[i])->rc, 2);
        if(0 != pthread_cond_init(&(task_data[i])->cond, NULL))
            TEST_ERROR;
        if(0 != pthread_mutex_init(&(task_data[i])->cond_mutex, NULL))
            TEST_ERROR;
        task_data[i]->failed = 0;
    } /* end for */

    /* Create tasks */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, NULL, task_data[0],
            free_op_data_worker) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[1], 0, NULL, 0, NULL, NULL, task_data[1],
            free_op_data_worker) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(helper_data->engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[2], 0, NULL, 0, NULL, NULL, task_data[2],
            free_op_data_worker) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(helper_data->engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify free_op_data has been called the correct number of times */
    if(OPA_load_int(&(task_data[0])->ncalls) != 0)
        TEST_ERROR;
    if(OPA_load_int(&(task_data[1])->ncalls) != 0)
        TEST_ERROR;
    if(OPA_load_int(&(task_data[2])->ncalls) != 0)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for condition signal so we know the free_op_data callback has been
     * called for each task */
    for(i = 0; i <= 2; i++) {
        if(0 != pthread_mutex_lock(&(task_data[i])->cond_mutex))
            TEST_ERROR;
        if(OPA_load_int(&(task_data[i])->ncalls) != 1)
            if(0 != pthread_cond_wait(&(task_data[i])->cond, &(task_data[i])->cond_mutex))
                TEST_ERROR;
        if(0 != pthread_mutex_unlock(&(task_data[i])->cond_mutex))
            TEST_ERROR;
    } /* end for */

    /* Verify free_op_data has been called the correct number of times (arguably
     * redundant, but may catch a strange bug that sees a thread calling
     * free_op_data more than once for a task) */
    if(OPA_load_int(&(task_data[0])->ncalls) != 1)
        TEST_ERROR;
    if(OPA_load_int(&(task_data[1])->ncalls) != 1)
        TEST_ERROR;
    if(OPA_load_int(&(task_data[2])->ncalls) != 1)
        TEST_ERROR;

    /* Free task data structs */
    for(i = 0; i <= 2; i++) {
        if(free_op_data_decr_ref(task_data[i]) != 0)
            TEST_ERROR;
        task_data[i] = NULL;
    } /* end for */


    /*
     * Test 3: Two normal tasks, one barrier task
     */
    /* Only run if not running in parallel, so barrier task doesn't interfere
     * with other tests */
    if(!(helper_data->parallel_mutex)) {
        /* Allocate and initialize task_data */
        for(i = 0; i <= 2; i++) {
            if(NULL == (task_data[i] = (free_op_data_t *)malloc(sizeof(free_op_data_t))))
                TEST_ERROR;
            OPA_store_int(&(task_data[i])->ncalls, 0);
            OPA_store_int(&(task_data[i])->rc, 2);
            if(0 != pthread_cond_init(&(task_data[i])->cond, NULL))
                TEST_ERROR;
            if(0 != pthread_mutex_init(&(task_data[i])->cond_mutex, NULL))
                TEST_ERROR;
            task_data[i]->failed = 0;
        } /* end for */

        /* Create tasks */
        if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, NULL, task_data[0],
                free_op_data_worker) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(helper_data->engine, task[1], 0, NULL, 0, NULL, NULL, task_data[1],
                free_op_data_worker) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEgenerate_task_id(helper_data->engine, &task[2]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_barrier_task(helper_data->engine, task[2], NULL, task_data[2],
                free_op_data_worker) != AXE_SUCCEED)
            TEST_ERROR;

        /* Wait for tasks to complete */
        if(AXEwait(helper_data->engine, task[0]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEwait(helper_data->engine, task[1]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEwait(helper_data->engine, task[2]) != AXE_SUCCEED)
            TEST_ERROR;

        /* Verify free_op_data has been called the correct number of times */
        if(OPA_load_int(&(task_data[0])->ncalls) != 0)
            TEST_ERROR;
        if(OPA_load_int(&(task_data[1])->ncalls) != 0)
            TEST_ERROR;
        if(OPA_load_int(&(task_data[2])->ncalls) != 0)
            TEST_ERROR;

        /* Close tasks */
        if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(helper_data->engine, task[1]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(helper_data->engine, task[2]) != AXE_SUCCEED)
            TEST_ERROR;

        /* Wait for condition signal so we know the free_op_data callback has been
         * called for each task */
        for(i = 0; i <= 2; i++) {
            if(0 != pthread_mutex_lock(&(task_data[i])->cond_mutex))
                TEST_ERROR;
            if(OPA_load_int(&(task_data[i])->ncalls) != 1)
                if(0 != pthread_cond_wait(&(task_data[i])->cond, &(task_data[i])->cond_mutex))
                    TEST_ERROR;
            if(0 != pthread_mutex_unlock(&(task_data[i])->cond_mutex))
                TEST_ERROR;
        } /* end for */

        /* Verify free_op_data has been called the correct number of times (arguably
         * redundant, but may catch a strange bug that sees a thread calling
         * free_op_data more than once for a task) */
        if(OPA_load_int(&(task_data[0])->ncalls) != 1)
            TEST_ERROR;
        if(OPA_load_int(&(task_data[1])->ncalls) != 1)
            TEST_ERROR;
        if(OPA_load_int(&(task_data[2])->ncalls) != 1)
            TEST_ERROR;

        /* Free task data structs */
        for(i = 0; i <= 2; i++) {
            if(free_op_data_decr_ref(task_data[i]) != 0)
                TEST_ERROR;
            task_data[i] = NULL;
        } /* end for */
    } /* end if */


    /*
     * Close
     */
    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_free_op_data_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_remove_helper
 *
 * Purpose:     Tests removing tasks from an engine.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
void
test_remove_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_task_t task[2];
    AXE_status_t status;
    AXE_remove_status_t remove_status;
    basic_task_t task_data[2];
    basic_task_shared_t shared_task_data;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_mutex_t cond_mutex;
    int i;

    /* Initialize mutexes and condition variables */
    if(0 != pthread_mutex_init(&mutex, NULL))
        TEST_ERROR;
    if(0 != pthread_cond_init(&cond, NULL))
        TEST_ERROR;
    if(0 != pthread_mutex_init(&cond_mutex, NULL))
        TEST_ERROR;

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
    } /* end for */


    /*
     * Test 1: Single task, attempt removing while in progress and complete
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 1;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].run_order = -1;
        task_data[i].cond_signal_sent = 0;
    } /* end for */
    task_data[0].mutex = &mutex;
    task_data[0].cond = &cond;
    task_data[0].cond_mutex = &cond_mutex;

    /* Lock mutex */
    if(0 != pthread_mutex_lock(task_data[0].mutex))
        TEST_ERROR;

    /* Create task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for condition signal so we know the task is in progress */
    if(0 != pthread_mutex_lock(task_data[0].cond_mutex))
        TEST_ERROR;
    if(task_data[0].cond_signal_sent == 0)
        if(0 != pthread_cond_wait(task_data[0].cond, task_data[0].cond_mutex))
            TEST_ERROR;
    if(0 != pthread_mutex_unlock(task_data[0].cond_mutex))
        TEST_ERROR;

    /* Try to remove the task.  Should return AXE_NOT_CANCELED. */
    if(AXEremove(helper_data->engine, task[0], &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_NOT_CANCELED)
        TEST_ERROR;

    /* Verify task status is AXE_TASK_RUNNING */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_RUNNING)
        TEST_ERROR;

    /* Unlock the main mutex to allow the task to proceed */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for task to complete */
    if(AXEwait(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Try to remove the task.  Should return AXE_ALL_DONE. */
    if(AXEremove(helper_data->engine, task[0], &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_ALL_DONE)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 1; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 1)
        TEST_ERROR;

    /* Close task */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
    } /* end for */


    /*
     * Test 2: Two task chain, try removing both
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 1;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].run_order = -1;
        task_data[i].cond_signal_sent = 0;
    } /* end for */
    task_data[0].mutex = &mutex;
    task_data[0].cond = &cond;
    task_data[0].cond_mutex = &cond_mutex;

    /* Lock mutex */
    if(0 != pthread_mutex_lock(task_data[0].mutex))
        TEST_ERROR;

    /* Create first task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second task */
    if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for condition signal so we know the task is in progress */
    if(0 != pthread_mutex_lock(task_data[0].cond_mutex))
        TEST_ERROR;
    if(task_data[0].cond_signal_sent == 0)
        if(0 != pthread_cond_wait(task_data[0].cond, task_data[0].cond_mutex))
            TEST_ERROR;
    if(0 != pthread_mutex_unlock(task_data[0].cond_mutex))
        TEST_ERROR;

    /* Verify task statuses */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_RUNNING)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_WAITING_FOR_PARENT)
        TEST_ERROR;

    /* Try to remove the first task.  Should fail. */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEremove(helper_data->engine, task[0], &remove_status) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Remove the second task.  Should return AXE_CANCELED. */
    if(AXEremove(helper_data->engine, task[1], &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_CANCELED)
        TEST_ERROR;

    /* Unlock the main mutex to allow the first task to proceed */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete.  Include wait on canceled task to make sure
     * wait correctly returns failure for canceled task. */
    if(AXEwait(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(helper_data->engine, task[1]) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Try againto remove the first task.  Should still fail. */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEremove(helper_data->engine, task[0], &remove_status) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_CANCELED)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 1; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 1)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 3: Two task chain with sufficient condition, try removing both
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 1;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].run_order = -1;
        task_data[i].cond_signal_sent = 0;
    } /* end for */
    task_data[0].mutex = &mutex;
    task_data[0].cond = &cond;
    task_data[0].cond_mutex = &cond_mutex;

    /* Lock mutex */
    if(0 != pthread_mutex_lock(task_data[0].mutex))
        TEST_ERROR;

    /* Create first task */
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second task */
    if(AXEgenerate_task_id(helper_data->engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[1], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for condition signal so we know the task is in progress */
    if(0 != pthread_mutex_lock(task_data[0].cond_mutex))
        TEST_ERROR;
    if(task_data[0].cond_signal_sent == 0)
        if(0 != pthread_cond_wait(task_data[0].cond, task_data[0].cond_mutex))
            TEST_ERROR;
    if(0 != pthread_mutex_unlock(task_data[0].cond_mutex))
        TEST_ERROR;

    /* Verify task statuses */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_RUNNING)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_WAITING_FOR_PARENT)
        TEST_ERROR;

    /* Try to remove the first task.  Should fail. */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEremove(helper_data->engine, task[0], &remove_status) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Remove the second task.  Should return AXE_CANCELED. */
    if(AXEremove(helper_data->engine, task[1], &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_CANCELED)
        TEST_ERROR;

    /* Unlock the main mutex to allow the first task to proceed */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete.  Include wait on canceled task to make sure
     * wait correctly returns failure for canceled task. */
    if(AXEwait(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(helper_data->engine, task[1]) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Try again to remove the first task.  Should still fail. */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEremove(helper_data->engine, task[0], &remove_status) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(helper_data->engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(helper_data->engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_CANCELED)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 1; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 1)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(helper_data->engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Close
     */
    /* Destroy mutexes and condition variables */
    if(0 != pthread_mutex_destroy(&mutex))
        TEST_ERROR;
    if(0 != pthread_cond_destroy(&cond))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&cond_mutex))
        TEST_ERROR;

    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    (void)pthread_mutex_destroy(&mutex);
    (void)pthread_cond_destroy(&cond);
    (void)pthread_mutex_destroy(&cond_mutex);

    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_remove_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_remove_all_helper
 *
 * Purpose:     Tests functionality of AXEremove_all().
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
void
test_remove_all_helper(AXE_engine_t _engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_engine_t engine;
    AXE_engine_attr_t engine_attr;
    AXE_task_t task[4];
    AXE_status_t status;
    AXE_remove_status_t remove_status;
    basic_task_t task_data[4];
    basic_task_shared_t shared_task_data;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_mutex_t cond_mutex;
    int i;

    /* Initialize mutexes and condition variables */
    if(0 != pthread_mutex_init(&mutex, NULL))
        TEST_ERROR;
    if(0 != pthread_cond_init(&cond, NULL))
        TEST_ERROR;
    if(0 != pthread_mutex_init(&cond_mutex, NULL))
        TEST_ERROR;

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
    } /* end for */

    /* Reserve threads for engine */
    MAX_NTHREADS_RESERVE(helper_data->num_threads, TEST_ERROR);
    
    /* Initialize engine attribute */
    if(AXEengine_attr_init(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create AXE engine */
    if(AXEset_num_threads(&engine_attr, helper_data->num_threads) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_engine(&engine, &engine_attr) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * All tests have a configuration of one parent with one two-task chain of
     * necessary children and a single sufficient child
     */
    /*
     * Test 1: Hold mutex on parent
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 1;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].run_order = -1;
        task_data[i].cond_signal_sent = 0;
    } /* end for */
    task_data[0].mutex = &mutex;

    /* Lock mutex */
    if(0 != pthread_mutex_lock(task_data[0].mutex))
        TEST_ERROR;

    /* Create parent task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create children */
    if(AXEgenerate_task_id(engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[2], 1, &task[1], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[3]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[3], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[3], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Remove all tasks.  Should return AXE_NOT_CANCELED or AXE_CANCELED. */
    if(AXEremove_all(engine, &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if((remove_status != AXE_NOT_CANCELED) && (remove_status != AXE_CANCELED))
        TEST_ERROR;

    /* Unlock the main mutex to allow the parent task to proceed */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for parent task to complete (may fail if task was canceled) */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    (void)AXEwait(engine, task[0]);
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify children are canceled and parent is either canceled or done */
    if(AXEget_status(engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status != AXE_TASK_DONE) && (status != AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 1; i <= 3; i++) {
        if(AXEget_status(engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_CANCELED)
            TEST_ERROR;
    } /* end for */

    /* Try to remove all the tasks.  Should return AXE_ALL_DONE. */
    if(AXEremove_all(engine, &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_ALL_DONE)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status != AXE_TASK_DONE) && (status != AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 1; i <= 3; i++) {
        if(AXEget_status(engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_CANCELED)
            TEST_ERROR;
    } /* end for */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    for(i = 1; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) > 1)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i < 4; i++)
        if(AXEfinish(engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
    } /* end for */


    /*
     * Test 2: Hold mutex on first necessary child
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 3;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].run_order = -1;
        task_data[i].cond_signal_sent = 0;
    } /* end for */
    task_data[1].mutex = &mutex;
    task_data[1].cond = &cond;
    task_data[1].cond_mutex = &cond_mutex;

    /* Lock mutex */
    if(0 != pthread_mutex_lock(task_data[1].mutex))
        TEST_ERROR;

    /* Create parent task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create children */
    if(AXEgenerate_task_id(engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[2], 1, &task[1], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[3]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[3], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[3], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for condition signal so we know the first child task is in progress
     */
    if(0 != pthread_mutex_lock(task_data[1].cond_mutex))
        TEST_ERROR;
    if(task_data[1].cond_signal_sent == 0)
        if(0 != pthread_cond_wait(task_data[1].cond, task_data[1].cond_mutex))
            TEST_ERROR;
    if(0 != pthread_mutex_unlock(task_data[1].cond_mutex))
        TEST_ERROR;

    /* Remove all tasks.  Should return AXE_NOT_CANCELED. */
    if(AXEremove_all(engine, &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_NOT_CANCELED)
        TEST_ERROR;

    /* Verify statuses */
    if(AXEget_status(engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_RUNNING)
        TEST_ERROR;
    if(AXEget_status(engine, task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_CANCELED)
        TEST_ERROR;
    if(AXEget_status(engine, task[3], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status != AXE_TASK_DONE) && (status != AXE_TASK_CANCELED)
            && (status != AXE_TASK_RUNNING))
        TEST_ERROR;

    /* Unlock the main mutex to allow the first child task to proceed */
    if(0 != pthread_mutex_unlock(task_data[1].mutex))
        TEST_ERROR;

    /* Wait for the child tasks to complete (sufficient child may fail if
     * it was canceled) */
    if(AXEwait(engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    (void)AXEwait(engine, task[3]);
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify statuses */
    if(AXEget_status(engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(engine, task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_CANCELED)
        TEST_ERROR;
    if(AXEget_status(engine, task[3], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status != AXE_TASK_DONE) && (status != AXE_TASK_CANCELED))
        TEST_ERROR;

    /* Try to remove all the tasks.  Should return AXE_ALL_DONE. */
    if(AXEremove_all(engine, &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_ALL_DONE)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(engine, task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_CANCELED)
        TEST_ERROR;
    if(AXEget_status(engine, task[3], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status != AXE_TASK_DONE) && (status != AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if((task_data[1].run_order < 1) || (task_data[1].run_order > 2))
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    if(task_data[2].run_order != -1)
        TEST_ERROR;
    if((task_data[3].run_order == 0) || (task_data[3].run_order > 2))
        TEST_ERROR;
    if((OPA_load_int(&shared_task_data.ncalls) < 2)
            || (OPA_load_int(&shared_task_data.ncalls) > 3))
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i < 4; i++)
        if(AXEfinish(engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
    } /* end for */


    /*
     * Test 3: Wait until all are complete
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 4;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].run_order = -1;
        task_data[i].cond_signal_sent = 0;
    } /* end for */

    /* Create parent task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create children */
    if(AXEgenerate_task_id(engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[2], 1, &task[1], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[3]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[3], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[3], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for the child tasks to complete  */
    if(AXEwait(engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(engine, task[3]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify statuses */
    if(AXEget_status(engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(engine, task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(engine, task[3], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;

    /* Try to remove all the tasks.  Should return AXE_ALL_DONE. */
    if(AXEremove_all(engine, &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_ALL_DONE)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(engine, task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(engine, task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(engine, task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(engine, task[3], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if((task_data[1].run_order < 1) || (task_data[1].run_order > 2))
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    if((task_data[2].run_order < 2) || (task_data[1].run_order > 3))
        TEST_ERROR;
    if(task_data[2].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[2].num_sufficient_parents != 0)
        TEST_ERROR;
    if((task_data[3].run_order < 1) || (task_data[3].run_order > 3))
        TEST_ERROR;
    if(task_data[3].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[3].num_sufficient_parents != 1)
        TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 4)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i < 4; i++)
        if(AXEfinish(engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;


    /*
     * Close
     */
    /* Terminate engine */
    AXE_test_exclude_close_on(engine);
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Release threads used by engine */
    MAX_NTHREADS_RELEASE(helper_data->num_threads, TEST_ERROR);

    /* Destroy engine attribute */
    if(AXEengine_attr_destroy(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Destroy mutexes and condition variables */
    if(0 != pthread_mutex_destroy(&mutex))
        TEST_ERROR;
    if(0 != pthread_cond_destroy(&cond))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&cond_mutex))
        TEST_ERROR;

    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    (void)AXEterminate_engine(engine, FALSE);
    MAX_NTHREADS_RELEASE(helper_data->num_threads, );

    (void)AXEengine_attr_destroy(&engine_attr);

    (void)pthread_mutex_destroy(&mutex);
    (void)pthread_cond_destroy(&cond);
    (void)pthread_mutex_destroy(&cond_mutex);

    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_remove_all_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_terminate_engine_helper
 *
 * Purpose:     Tests functionality of AXEterminate_engine(), with
 *              wait_all set to both TRUE and FALSE.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
void
test_terminate_engine_helper(AXE_engine_t _engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_engine_t engine;
    AXE_engine_attr_t engine_attr;
    _Bool engine_init = FALSE;
    AXE_task_t task[4];
    basic_task_t task_data[4];
    basic_task_shared_t shared_task_data;
    int i;

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
    } /* end for */

    /* Initialize engine attribute */
    if(AXEengine_attr_init(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Set number of threads on engine attribute */
    if(AXEset_num_threads(&engine_attr, helper_data->num_threads) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * All tests have a configuration of one parent with one two-task chain of
     * necessary children and a single sufficient child
     */
    /*
     * Test 1: Wait all
     */
    /* Reserve threads for engine */
    MAX_NTHREADS_RESERVE(helper_data->num_threads, TEST_ERROR);

    /* Create AXE engine */
    if(AXEcreate_engine(&engine, &engine_attr) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = TRUE;

    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 4;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create parent task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create children */
    if(AXEgenerate_task_id(engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[2], 1, &task[1], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[3]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[3], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[3], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Terminate engine, with wait_all set to TRUE */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = FALSE;

    /* Release threads used by engine */
    MAX_NTHREADS_RELEASE(helper_data->num_threads, TEST_ERROR);

    /* Verify results - all tasks should have completed */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if((task_data[1].run_order < 1) || (task_data[1].run_order > 2))
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    if((task_data[2].run_order < 2) || (task_data[1].run_order > 3))
        TEST_ERROR;
    if(task_data[2].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[2].num_sufficient_parents != 0)
        TEST_ERROR;
    if((task_data[3].run_order < 1) || (task_data[3].run_order > 3))
        TEST_ERROR;
    if(task_data[3].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[3].num_sufficient_parents != 1)
        TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 4)
        TEST_ERROR;


    /*
     * Test 2: No wait all
     */
    /* Reserve threads for engine */
    MAX_NTHREADS_RESERVE(helper_data->num_threads, TEST_ERROR);

    /* Create AXE engine */
    if(AXEcreate_engine(&engine, &engine_attr) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = TRUE;

    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 4;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create parent task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create children */
    if(AXEgenerate_task_id(engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[2], 1, &task[1], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[3]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[3], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[3], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Terminate engine, with wait_all set to FALSE */
    if(AXEterminate_engine(engine, FALSE) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = FALSE;

    /* Release threads used by engine */
    MAX_NTHREADS_RELEASE(helper_data->num_threads, TEST_ERROR);

    /* Verify results */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order == -1) {
        for(i = 1; i <= 3; i++)
            if(task_data[i].run_order != -1)
                TEST_ERROR;
    } /* end if */
    else {
        if(task_data[0].run_order != 0)
            TEST_ERROR;
        if(task_data[0].num_necessary_parents != 0)
            TEST_ERROR;
        if(task_data[0].num_sufficient_parents != 0)
            TEST_ERROR;
        if(task_data[1].run_order == -1) {
            if(task_data[2].run_order != -1)
                TEST_ERROR;
        } /* end if */
        else {
            if((task_data[1].run_order < 1) || (task_data[1].run_order > 2))
                TEST_ERROR;
            if(task_data[1].num_necessary_parents != 1)
                TEST_ERROR;
            if(task_data[1].num_sufficient_parents != 0)
                TEST_ERROR;
            if(task_data[2].run_order != -1) {
                if((task_data[2].run_order < 2) || (task_data[1].run_order > 3))
                    TEST_ERROR;
                if(task_data[2].num_necessary_parents != 1)
                    TEST_ERROR;
                if(task_data[2].num_sufficient_parents != 0)
                    TEST_ERROR;
            } /* end if */
        } /* end else */
        if(task_data[3].run_order != -1) {
            if((task_data[3].run_order < 1) || (task_data[3].run_order > 3))
                TEST_ERROR;
            if(task_data[3].num_necessary_parents != 0)
                TEST_ERROR;
            if(task_data[3].num_sufficient_parents != 1)
                TEST_ERROR;
        } /* end if */
    } /* end else */
    if(OPA_load_int(&shared_task_data.ncalls) > 4)
        TEST_ERROR;


    /*
     * Test 3: Wait all, with handles closed before the terminate and
     * exlude_close set to on
     */
    /* Reserve threads for engine */
    MAX_NTHREADS_RESERVE(helper_data->num_threads, TEST_ERROR);

    /* Create AXE engine */
    if(AXEcreate_engine(&engine, &engine_attr) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = TRUE;

    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 4;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create parent task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create children */
    if(AXEgenerate_task_id(engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[2], 1, &task[1], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(engine, task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[3]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[3], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[3], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(engine, task[3]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Close handles so we can test that all tasks are internally closed and
     * freed */
    if(AXEfinish(engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Terminate engine, with wait_all set to TRUE */
    AXE_test_exclude_close_on(engine);
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = FALSE;

    /* Release threads used by engine */
    MAX_NTHREADS_RELEASE(helper_data->num_threads, TEST_ERROR);

    /* Verify results - all tasks should have completed */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    if((task_data[1].run_order < 1) || (task_data[1].run_order > 2))
        TEST_ERROR;
    if(task_data[1].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[1].num_sufficient_parents != 0)
        TEST_ERROR;
    if((task_data[2].run_order < 2) || (task_data[1].run_order > 3))
        TEST_ERROR;
    if(task_data[2].num_necessary_parents != 1)
        TEST_ERROR;
    if(task_data[2].num_sufficient_parents != 0)
        TEST_ERROR;
    if((task_data[3].run_order < 1) || (task_data[3].run_order > 3))
        TEST_ERROR;
    if(task_data[3].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[3].num_sufficient_parents != 1)
        TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 4)
        TEST_ERROR;


    /*
     * Close
     */
    /* Destroy engine attribute */
    if(AXEengine_attr_destroy(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    if(engine_init) {
        (void)AXEterminate_engine(engine, FALSE);
        MAX_NTHREADS_RELEASE(helper_data->num_threads, );
    } /* end if */

    (void)AXEengine_attr_destroy(&engine_attr);

    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_terminate_engine_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_num_threads_helper
 *
 * Purpose:     Tests that there are always exactly the specified number
 *              of threads available in an AXE engine.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
void
test_num_threads_helper(AXE_engine_t _engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_engine_t engine;
    AXE_engine_attr_t engine_attr;
    AXE_task_t task[5];
    AXE_status_t status;
    AXE_remove_status_t remove_status;
    basic_task_t task_data[5];
    basic_task_shared_t shared_task_data;
    pthread_mutex_t mutex1, mutex2;
    pthread_cond_t cond;
    pthread_mutex_t cond_mutex;
    int nrunning;
    int sched_i;
    int i;

    /* Initialize mutexes and condition variable */
    if(0 != pthread_mutex_init(&mutex1, NULL))
        TEST_ERROR;
    if(0 != pthread_mutex_init(&mutex2, NULL))
        TEST_ERROR;
    if(0 != pthread_cond_init(&cond, NULL))
        TEST_ERROR;
    if(0 != pthread_mutex_init(&cond_mutex, NULL))
        TEST_ERROR;

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
        task_data[i].cond_signal_sent = 0;
    } /* end for */

    /* Reserve threads for engine */
    MAX_NTHREADS_RESERVE(2, TEST_ERROR);

    /* Initialize engine attribute */
    if(AXEengine_attr_init(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create AXE engine with 2 threads */
    if(AXEset_num_threads(&engine_attr, 2) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_engine(&engine, &engine_attr) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 1: Four tasks, two threads.  Verify that only two threads execute,
     * verify that we can cancel scheduled tasks.
     */

    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 3;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i <= 3; i++) {
        task_data[i].run_order = -1;
        task_data[i].mutex = &mutex1;
        task_data[i].cond = &cond;
        task_data[i].cond_mutex = &cond_mutex;
        task_data[i].cond_signal_sent = 0;
    } /* end for */

    /* Lock mutex */
    if(0 != pthread_mutex_lock(&mutex1))
        TEST_ERROR;

    /* Create tasks */
    for(i = 0; i <= 3; i++) {
        if(AXEgenerate_task_id(engine, &task[i]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(engine, task[i], 0, NULL, 0, NULL, basic_task_worker,
                &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;
    } /* end for */

    /* Repeatedly scan task_data array and wait for signal until 2 tasks are
     * executing.  Do so while holding condition mutex so cond_signal_sent field
     * is useful. */
    if(0 != pthread_mutex_lock(&cond_mutex))
        TEST_ERROR;

    do {
        nrunning = 0;
        for(i = 0; i <= 3; i++) {
            /* Make sure if the signal was sent the task is running */
            AXEget_status(engine, task[i], &status);
            if(task_data[i].cond_signal_sent != 0) {
                if(status != AXE_TASK_RUNNING)
                    TEST_ERROR;
                nrunning++;
            } /* end if */
            else
                if((status != AXE_WAITING_FOR_PARENT)
                        && (status != AXE_TASK_SCHEDULED)
                        && (status != AXE_TASK_RUNNING))
                    TEST_ERROR;
        } /* end for */

        /* If 2 tasks are running we can exit the loop */
        if(nrunning == 2)
            break;
        if(nrunning > 2)
            TEST_ERROR;

        /* Wait for signal that a task has begun */
        if(0 != pthread_cond_wait(&cond, &cond_mutex))
            TEST_ERROR;
    } while(1);

    /* Unlock condition mutex */
    if(0 != pthread_mutex_unlock(&cond_mutex))
        TEST_ERROR;

    /* Do one more pass over task_data, verifying that only 2 tasks are still
     * running, and the others are scheduled.  Save the index of a scheduled
     * task. */
    nrunning = 0;
    sched_i = -1;
    for(i = 0; i <= 3; i++) {
        /* Make sure if the signal was sent the task is running */
        AXEget_status(engine, task[i], &status);
        if(task_data[i].cond_signal_sent != 0) {
            if(status != AXE_TASK_RUNNING)
                TEST_ERROR;
            nrunning++;
        } /* end if */
        else {
            if(status != AXE_TASK_SCHEDULED)
                TEST_ERROR;
            sched_i = i;
        } /* end else */
    } /* end for */
    if(nrunning != 2)
        TEST_ERROR;
    if((sched_i < 0) || (sched_i > 3))
        TEST_ERROR;

    /* Remove the scheduled task */
    if(AXEremove(engine, task[sched_i], &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_CANCELED)
        TEST_ERROR;

    /* Unlock main mutex */
    if(0 != pthread_mutex_unlock(&mutex1))
        TEST_ERROR;

    /* Wait for tasks to complete */
    for(i = 0; i <= 3; i++)
        if(i != sched_i)
            if(AXEwait(engine, task[i]) != AXE_SUCCEED)
                TEST_ERROR;

    /* Verify results */
    for(i = 0; i <= 3; i++) {
        if(AXEget_status(engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(i == sched_i) {
            if(status != AXE_TASK_CANCELED)
                TEST_ERROR;
        } /* end if */
        else
            if(status != AXE_TASK_DONE)
                TEST_ERROR;
    } /* end for */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    for(i = 0; i <= 3; i++) {
        if(i == sched_i) {
            if(task_data[i].run_order != -1)
                TEST_ERROR;
        } /* end if */
        else {
            if((task_data[i].run_order < 0) || (task_data[i].run_order > 2))
                TEST_ERROR;
            if(task_data[i].num_necessary_parents != 0)
                TEST_ERROR;
            if(task_data[i].num_sufficient_parents != 0)
                TEST_ERROR;
        } /* end else */
    } /* end for */
    if(OPA_load_int(&shared_task_data.ncalls) != 3)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i <= 3; i++)
        if(AXEfinish(engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;


    /*
     * Test 2: One parent, four children, two threads.  Verify that only two
     * threads execute, verify that we can cancel scheduled tasks.
     */

    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 4;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i <= 4; i++) {
        task_data[i].run_order = -1;
        task_data[i].cond = &cond;
        task_data[i].cond_mutex = &cond_mutex;
        task_data[i].cond_signal_sent = 0;
    } /* end for */
    task_data[0].mutex = &mutex1;
    for(i = 1; i <= 4; i++)
        task_data[i].mutex = &mutex2;

    /* Lock mutexes */
    if(0 != pthread_mutex_lock(&mutex1))
        TEST_ERROR;
    if(0 != pthread_mutex_lock(&mutex2))
        TEST_ERROR;

    /* Create first task */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child tasks */
    for(i = 1; i <= 4; i++) {
        if(AXEgenerate_task_id(engine, &task[i]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(engine, task[i], 1, &task[0], 0, NULL,
                basic_task_worker, &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;
    } /* end for */

    /* Wait for condition signal so we know the parent task is in progress */
    if(0 != pthread_mutex_lock(&cond_mutex))
        TEST_ERROR;
    if(task_data[0].cond_signal_sent == 0)
        if(0 != pthread_cond_wait(&cond, &cond_mutex))
            TEST_ERROR;
    if(0 != pthread_mutex_unlock(&cond_mutex))
        TEST_ERROR;

    /* Unlock parent mutex to allow parent to continue */
    if(0 != pthread_mutex_unlock(&mutex1))
        TEST_ERROR;

    /* Repeatedly scan task_data array and wait for signal until 2 tasks are
     * executing.  Do so while holding condition mutex so cond_signal_sent field
     * is useful. */
    if(0 != pthread_mutex_lock(&cond_mutex))
        TEST_ERROR;

    do {
        nrunning = 0;
        for(i = 1; i <= 4; i++) {
            /* Make sure if the signal was sent the task is running */
            AXEget_status(engine, task[i], &status);
            if(task_data[i].cond_signal_sent != 0) {
                if(status != AXE_TASK_RUNNING)
                    TEST_ERROR;
                nrunning++;
            } /* end if */
            else
                if((status != AXE_WAITING_FOR_PARENT)
                        && (status != AXE_TASK_SCHEDULED)
                        && (status != AXE_TASK_RUNNING))
                    TEST_ERROR;
        } /* end for */

        /* If 2 tasks are running we can exit the loop */
        if(nrunning == 2)
            break;
        if(nrunning > 2)
            TEST_ERROR;

        /* Wait for signal that a task has begun */
        if(0 != pthread_cond_wait(&cond, &cond_mutex))
            TEST_ERROR;
    } while(1);

    /* Unlock condition mutex */
    if(0 != pthread_mutex_unlock(&cond_mutex))
        TEST_ERROR;

    /* Do one more pass over task_data, verifying that only 2 tasks are still
     * running, and the others are scheduled.  Save the index of a scheduled
     * task. */
    nrunning = 0;
    sched_i = -1;
    for(i = 1; i <= 4; i++) {
        /* Make sure if the signal was sent the task is running */
        AXEget_status(engine, task[i], &status);
        if(task_data[i].cond_signal_sent != 0) {
            if(status != AXE_TASK_RUNNING)
                TEST_ERROR;
            nrunning++;
        } /* end if */
        else {
            if(status != AXE_TASK_SCHEDULED)
                TEST_ERROR;
            sched_i = i;
        } /* end else */
    } /* end for */
    if(nrunning != 2)
        TEST_ERROR;
    if((sched_i < 0) || (sched_i > 4))
        TEST_ERROR;

    /* Remove the scheduled task */
    if(AXEremove(engine, task[sched_i], &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_CANCELED)
        TEST_ERROR;

    /* Unlock child mutex */
    if(0 != pthread_mutex_unlock(&mutex2))
        TEST_ERROR;

    /* Wait for tasks to complete */
    for(i = 1; i <= 4; i++)
        if(i != sched_i)
            if(AXEwait(engine, task[i]) != AXE_SUCCEED)
                TEST_ERROR;

    /* Verify results */
    for(i = 0; i <= 4; i++) {
        if(AXEget_status(engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(i == sched_i) {
            if(status != AXE_TASK_CANCELED)
                TEST_ERROR;
        } /* end if */
        else
            if(status != AXE_TASK_DONE)
                TEST_ERROR;
    } /* end for */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[0].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[0].num_sufficient_parents != 0)
        TEST_ERROR;
    for(i = 1; i <= 4; i++) {
        if(i == sched_i) {
            if(task_data[i].run_order != -1)
                TEST_ERROR;
        } /* end if */
        else {
            if((task_data[i].run_order < 1) || (task_data[i].run_order > 3))
                TEST_ERROR;
            if(task_data[i].num_necessary_parents != 1)
                TEST_ERROR;
            if(task_data[i].num_sufficient_parents != 0)
                TEST_ERROR;
        } /* end else */
    } /* end for */
    if(OPA_load_int(&shared_task_data.ncalls) != 4)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i <= 4; i++)
        if(AXEfinish(engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;


    /*
     * Close
     */
    /* Terminate engine */
    AXE_test_exclude_close_on(engine);
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Release threads used by engine */
    MAX_NTHREADS_RELEASE(2, TEST_ERROR);

    /* Destroy engine attribute */
    if(AXEengine_attr_destroy(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Destroy mutexes and condition variables */
    if(0 != pthread_mutex_destroy(&mutex1))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&mutex2))
        TEST_ERROR;
    if(0 != pthread_cond_destroy(&cond))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&cond_mutex))
        TEST_ERROR;

    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    (void)AXEterminate_engine(engine, FALSE);
    MAX_NTHREADS_RELEASE(2, );

    (void)AXEengine_attr_destroy(&engine_attr);

    (void)pthread_mutex_destroy(&mutex1);
    (void)pthread_mutex_destroy(&mutex2);
    (void)pthread_cond_destroy(&cond);
    (void)pthread_mutex_destroy(&cond_mutex);

    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_num_threads_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_fractal_helper
 *
 * Purpose:     Creates a task that creates new tasks as necessary
 *              children of itself, until a certain number of tasks are
 *              created.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
/* Data shared between all tasks */
typedef struct fractal_task_shared_t {
    OPA_int_t num_tasks_left_start; /* Number of tasks left to create */
    OPA_int_t num_tasks_left_end; /* Number of tasks left to finish */
    pthread_cond_t cond;        /* Condition variable for signaling main thread when all tasks are complete */
    pthread_mutex_t cond_mutex; /* Mutex associated with cond */
} fractal_task_shared_t;

/* Data for a single task */
typedef struct fractal_task_t {
    fractal_task_shared_t *shared; /* Link to shared data */
    AXE_task_t this_task;       /* Handle for this task */
    struct fractal_task_t *child[FRACTAL_NCHILDREN]; /* Links to data for child tasks */
    int failed;                 /* Whether this task has failed */
} fractal_task_t;


/* Task worker function.  Creates children if there are more tasks to create and
 * sends signal if this was the last task to complete */
void
fractal_task_worker(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_task_data)
{
    fractal_task_t *task_data = (fractal_task_t *)_task_data;
    int i, j;

    assert(task_data);
    assert(task_data->shared);

    /* Make sure there is at most one necessary parent, and no sufficient
     * parents */
    if((num_necessary_parents > 1) || (num_sufficient_parents != 0))
        task_data->failed = 1;

    /* Decrement ref counts on parent arrays, as required */
    if(num_necessary_parents == 1) {
        if(AXEfinish(engine, necessary_parents[0]) != AXE_SUCCEED)
            task_data->failed = 1;
    } /* end if */

    /* Iterate over children */
    for(i = 0; i < FRACTAL_NCHILDREN; i++) {
        /* Fetch and decrement the number of tasks left to launch.  If there are
         * no more tasks to launch, reset the number of tasks left to launch and
         * do not launch any more children. */
        if(OPA_fetch_and_decr_int(&task_data->shared->num_tasks_left_start) <= 0) {
            OPA_incr_int(&task_data->shared->num_tasks_left_start);
            break;
        } /* end if */
        else {
            /* Init task struct */
            if(NULL == (task_data->child[i] = (fractal_task_t *)malloc(sizeof(fractal_task_t)))) {
                task_data->failed = 1;
                return;
            } /* end if */
            task_data->child[i]->shared = task_data->shared;
            for(j = 0; j < FRACTAL_NCHILDREN; j++)
                task_data->child[i]->child[j] = NULL;
            task_data->child[i]->failed = 0;

            /* Create task */
            if(AXEgenerate_task_id(engine, &task_data->child[i]->this_task)
                    != AXE_SUCCEED)
                task_data->failed = 1;
            if(AXEcreate_task(engine, task_data->child[i]->this_task, 1,
                    &task_data->this_task, 0, NULL, fractal_task_worker,
                    task_data->child[i], NULL) != AXE_SUCCEED)
                task_data->failed = 1;
        } /* end else */
    } /* end for */

    /* Close this task */
    if(AXEfinish(engine, task_data->this_task) != AXE_SUCCEED)
        task_data->failed = 1;

    /* Decrement and test the number of tasks left to finish.  If this was the
     * last task to finish, send the signal to wake up the launcher.  Safe to
     * lock the mutex after the decr-and-test because num_tasks_left_end never
     * moves from a signaled to not-signaled state. */
    if(OPA_decr_and_test_int(&task_data->shared->num_tasks_left_end)) {
        if(0 != pthread_mutex_lock(&task_data->shared->cond_mutex))
            task_data->failed = 1;
        if(0 != pthread_cond_signal(&task_data->shared->cond))
            task_data->failed = 1;
        if(0 != pthread_mutex_unlock(&task_data->shared->cond_mutex))
            task_data->failed = 1;
    } /* end if */

    return;
} /* end fractal_task_worker() */


/* Function to recursively verify that all task data structs report success,
 * count the number of task data structs, and free them */
int
fractal_verify_free(fractal_task_t *task_data, int *num_tasks)
{
    int i;
    int ret_value = 0;

    assert(task_data);
    assert(num_tasks);

    /* Recurse into children */
    for(i = 0; i < FRACTAL_NCHILDREN; i++)
        if(task_data->child[i])
            ret_value += fractal_verify_free(task_data->child[i], num_tasks);

    /* Check for failure */
    ret_value += task_data->failed;

    /* Add this task to the count */
    (*num_tasks)++;

    /* Free task data */
    free(task_data);

    return ret_value;
} /* end fractal_verify_free() */


/* Main test helper function */
void
test_fractal_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    fractal_task_t *parent_task_data;
    fractal_task_shared_t shared_task_data;
    int num_tasks;
    int i;

    /* Initialize shared task data struct */
    OPA_store_int(&shared_task_data.num_tasks_left_start, FRACTAL_NTASKS);
    OPA_store_int(&shared_task_data.num_tasks_left_end, FRACTAL_NTASKS);
    if(0 != pthread_cond_init(&shared_task_data.cond, NULL))
        TEST_ERROR;
    if(0 != pthread_mutex_init(&shared_task_data.cond_mutex, NULL))
        TEST_ERROR;

    /* Initialize parent task data struct */
    if(NULL == (parent_task_data = (fractal_task_t *)malloc(sizeof(fractal_task_t))))
        TEST_ERROR;
    parent_task_data->shared = &shared_task_data;
    for(i = 0; i < FRACTAL_NCHILDREN; i++)
        parent_task_data->child[i] = NULL;
    parent_task_data->failed = 0;

    /* Create parent task */
    OPA_decr_int(&shared_task_data.num_tasks_left_start);
    if(AXEgenerate_task_id(helper_data->engine, &parent_task_data->this_task) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, parent_task_data->this_task, 0, NULL, 0, NULL,
            fractal_task_worker, parent_task_data, NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for condition signal that all tasks finished */
    if(0 != pthread_mutex_lock(&shared_task_data.cond_mutex))
        TEST_ERROR;
    if(OPA_load_int(&shared_task_data.num_tasks_left_end) > 0)
        if(0 != pthread_cond_wait(&shared_task_data.cond, &shared_task_data.cond_mutex))
            TEST_ERROR;
    if(0 != pthread_mutex_unlock(&shared_task_data.cond_mutex))
        TEST_ERROR;

    /* Verify results */
    if(OPA_load_int(&shared_task_data.num_tasks_left_start) != 0)
        TEST_ERROR;
    if(OPA_load_int(&shared_task_data.num_tasks_left_end) != 0)
        TEST_ERROR;
    num_tasks = 0;
    if(fractal_verify_free(parent_task_data, &num_tasks) != 0)
        TEST_ERROR;
    if(num_tasks != FRACTAL_NTASKS)
        TEST_ERROR;

    /*
     * Close
     */
    /* Destroy mutex and condition variable */
    if(0 != pthread_cond_destroy(&shared_task_data.cond))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&shared_task_data.cond_mutex))
        TEST_ERROR;

    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    (void)pthread_cond_destroy(&shared_task_data.cond);
    (void)pthread_mutex_destroy(&shared_task_data.cond_mutex);

    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_fractal_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_fractal_nodep_helper
 *
 * Purpose:     Creates a task that creates new tasks, until a certain
 *              number of tasks are created.  Similar to
 *              test_fractal_helper but does not create dependencies or
 *              request task handles.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              March 11, 2013
 *
 *-------------------------------------------------------------------------
 */
/* Data shared between all tasks */
typedef struct fractal_nodep_task_t {
    OPA_int_t num_tasks_left_start; /* Number of tasks left to create */
    OPA_int_t num_tasks_left_end; /* Number of tasks left to finish */
    OPA_int_t failed;           /* Number of failures */
    pthread_cond_t cond;        /* Condition variable for signaling main thread when all tasks are complete */
    pthread_mutex_t cond_mutex; /* Mutex associated with cond */
} fractal_nodep_task_t;


/* Task worker function.  Creates children if there are more tasks to create and
 * sends signal if this was the last task to complete */
void
fractal_nodep_task_worker(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_task_data)
{
    fractal_nodep_task_t *task_data = (fractal_nodep_task_t *)_task_data;
    AXE_task_t tmp_task;
    int i;

    assert(task_data);

    /* Make sure there are no parents */
    if((num_necessary_parents != 0) || (num_sufficient_parents != 0))
        OPA_incr_int(&task_data->failed);

    /* Iterate over children */
    for(i = 0; i < FRACTAL_NODEP_NCHILDREN; i++) {
        /* Fetch and decrement the number of tasks left to launch.  If there are
         * no more tasks to launch, reset the number of tasks left to launch and
         * do not launch any more children. */
        if(OPA_fetch_and_decr_int(&task_data->num_tasks_left_start) <= 0) {
            OPA_incr_int(&task_data->num_tasks_left_start);
            break;
        } /* end if */
        else {
            /* Create child task */
            if(AXEgenerate_task_id(engine, &tmp_task) != AXE_SUCCEED)
                OPA_incr_int(&task_data->failed);
            if(AXEcreate_task(engine, tmp_task, 0, NULL, 0, NULL,
                    fractal_nodep_task_worker, task_data, NULL) != AXE_SUCCEED)
                OPA_incr_int(&task_data->failed);
            if(AXEfinish(engine, tmp_task) != AXE_SUCCEED)
                OPA_incr_int(&task_data->failed);
        } /* end else */
    } /* end for */

    /* Decrement and test the number of tasks left to finish.  If this was the
     * last task to finish, send the signal to wake up the launcher.  Safe to
     * lock the mutex after the decr-and-test because num_tasks_left_end never
     * moves from a signaled to not-signaled state. */
    if(OPA_decr_and_test_int(&task_data->num_tasks_left_end)) {
        if(0 != pthread_mutex_lock(&task_data->cond_mutex))
            OPA_incr_int(&task_data->failed);
        if(0 != pthread_cond_signal(&task_data->cond))
            OPA_incr_int(&task_data->failed);
        if(0 != pthread_mutex_unlock(&task_data->cond_mutex))
            OPA_incr_int(&task_data->failed);
    } /* end if */

    return;
} /* end fractal_nodep_task_worker() */


/* Main test helper function */
void
test_fractal_nodep_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    fractal_nodep_task_t task_data;
    AXE_task_t tmp_task;

    /* Initialize shared task data struct */
    OPA_store_int(&task_data.num_tasks_left_start, FRACTAL_NODEP_NTASKS);
    OPA_store_int(&task_data.num_tasks_left_end, FRACTAL_NODEP_NTASKS);
    OPA_store_int(&task_data.failed, 0);
    if(0 != pthread_cond_init(&task_data.cond, NULL))
        TEST_ERROR;
    if(0 != pthread_mutex_init(&task_data.cond_mutex, NULL))
        TEST_ERROR;

    /* Create parent task */
    OPA_decr_int(&task_data.num_tasks_left_start);
    if(AXEgenerate_task_id(helper_data->engine, &tmp_task) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, tmp_task, 0, NULL, 0, NULL,
            fractal_nodep_task_worker, &task_data, NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(helper_data->engine, tmp_task) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for condition signal that all tasks finished */
    if(0 != pthread_mutex_lock(&task_data.cond_mutex))
        TEST_ERROR;
    if(OPA_load_int(&task_data.num_tasks_left_end) > 0)
        if(0 != pthread_cond_wait(&task_data.cond, &task_data.cond_mutex))
            TEST_ERROR;
    if(0 != pthread_mutex_unlock(&task_data.cond_mutex))
        TEST_ERROR;

    /* Verify results */
    if(OPA_load_int(&task_data.num_tasks_left_start) != 0)
        TEST_ERROR;
    if(OPA_load_int(&task_data.num_tasks_left_end) != 0)
        TEST_ERROR;
    if(OPA_load_int(&task_data.failed) != 0)
        TEST_ERROR;

    /*
     * Close
     */
    /* Destroy mutex and condition variable */
    if(0 != pthread_cond_destroy(&task_data.cond))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&task_data.cond_mutex))
        TEST_ERROR;

    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    (void)pthread_cond_destroy(&task_data.cond);
    (void)pthread_mutex_destroy(&task_data.cond_mutex);

    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_fractal_nodep_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_create_remove_helper
 *
 * Purpose:     Runs 2 threads simultaneously (within the engine), one of
 *              which adds new tasks and the other which cancels these
 *              tasks.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              March 25, 2013
 *
 *-------------------------------------------------------------------------
 */
/* Data shared between all remove helper tasks */
typedef struct create_remove_shared_t {
    OPA_ptr_t child_task;       /* Pointer to child task (changes) */
    OPA_int_t num_cancels;      /* Number of tasks successfully canceled */
    OPA_int_t num_failed;       /* Number of helper threads failed */
    OPA_int_t shutdown;         /* Whether to shut down */
} create_remove_shared_t;


/* Secondary helper function.  Constantly tries to remove */
void
create_remove_remove_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_task_data)
{
    create_remove_shared_t *helper_data = (create_remove_shared_t *)_task_data;
    AXE_remove_status_t remove_status;
    AXE_error_t ret;

    assert(helper_data);

    /* Loop until told to shut dbown */
    while(!OPA_load_int(&helper_data->shutdown)) {
        /* Try to cancel the child task */
        if(AXEbegin_try() != AXE_SUCCEED)
            OPA_incr_int(&helper_data->num_failed);
        ret = AXEremove(engine, *(AXE_task_t *)OPA_load_ptr(&helper_data->child_task), &remove_status);
        if(AXEend_try() != AXE_SUCCEED)
            OPA_incr_int(&helper_data->num_failed);

        /* Check if we canceled the task, record it if we did */
        if((ret == AXE_SUCCEED) && (remove_status == AXE_CANCELED))
            OPA_incr_int(&helper_data->num_cancels);
    } /* end while */

    /* Make sure there are no parents */
    if((num_necessary_parents != 0) || (num_sufficient_parents != 0))
        OPA_incr_int(&helper_data->num_failed);

    return;
} /* end create_remove_remove_helper() */


/* Main test helper function */
void
test_create_remove_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    create_remove_shared_t int_helper_data;
    basic_task_t *task_data = NULL;
    basic_task_shared_t shared_task_data;
    AXE_task_t *task = NULL;
    AXE_task_t remove_helper_task;
    int total_ncalls;
    int last_run_order;
    AXE_status_t status;
    AXE_status_t last_status;
    AXE_error_t ret;
    int i;

    /* Initialize int_helper_data */
    OPA_store_int(&int_helper_data.num_cancels, 0);
    OPA_store_int(&int_helper_data.num_failed, 0);
    OPA_store_int(&int_helper_data.shutdown, FALSE);

    /* Allocate task_data */
    if(NULL == (task_data = (basic_task_t *)malloc(CREATE_REMOVE_NTASKS * sizeof(basic_task_t))))
        TEST_ERROR;

    /* Allocate task */
    if(NULL == (task = (AXE_task_t *)malloc(CREATE_REMOVE_NTASKS * sizeof(AXE_task_t))))
        TEST_ERROR;

    /* Initialize shared_task_data */
    shared_task_data.max_ncalls = CREATE_REMOVE_NTASKS;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Create first task (do so before launching remove helper so child_task is
     * initialized */
    task_data[0].shared = &shared_task_data;
    task_data[0].failed = 0;
    task_data[0].run_order = -1;
    task_data[0].mutex = NULL;
    task_data[0].cond = NULL;
    task_data[0].cond_mutex = NULL;
    task_data[0].cond_signal_sent = 0;
    if(AXEgenerate_task_id(helper_data->engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, task[0], 0, NULL, 0, NULL, basic_task_worker, &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    OPA_store_ptr(&int_helper_data.child_task, &task[0]);

    /* Launch remove helper */
    if(AXEgenerate_task_id(helper_data->engine, &remove_helper_task) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(helper_data->engine, remove_helper_task, 0, NULL, 0, NULL, create_remove_remove_helper, &int_helper_data, NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Launch CREATE_REMOVE_NTASKS tasks, keeping int_helper_data.child_task up
     * to date */
    for(i = 1; i < CREATE_REMOVE_NTASKS; i++) {
        /* Initialize task data */
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].run_order = -1;
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
        task_data[i].cond_signal_sent = 0;

        /* Try to launch task with parent.  If it fails (due to parent being
         * canceled), launch without parent.  Use cond_signal_sent field to keep
         * track of which tasks have no parents (1 == no parent). */
        if(AXEgenerate_task_id(helper_data->engine, &task[i]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEbegin_try() != AXE_SUCCEED)
            TEST_ERROR;
        ret = AXEcreate_task(helper_data->engine, task[i], 1, &task[i - 1], 0, NULL, basic_task_worker, &task_data[i], NULL);
        if(AXEend_try() != AXE_SUCCEED)
            TEST_ERROR;
        if(ret != AXE_SUCCEED) {
            task_data[i].cond_signal_sent = 1;
            if(AXEgenerate_task_id(helper_data->engine, &task[i]) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(helper_data->engine, task[i], 0, NULL, 0, NULL, basic_task_worker, &task_data[i], NULL) != AXE_SUCCEED)
                TEST_ERROR;
        } /* end if */

        /* Update int_helper_data.child_task */
        OPA_store_ptr(&int_helper_data.child_task, &task[i]);
    } /* end for */

    /* Send signal to shut down remove helper */
    OPA_store_int(&int_helper_data.shutdown, TRUE);

    /* Wait for tasks and remove helper to complete.  Note task may be canceled.
     */
    if(AXEwait(helper_data->engine, remove_helper_task) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    for(i = 0; i < CREATE_REMOVE_NTASKS; i++)
        (void)AXEwait(helper_data->engine, task[i]);
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /*
     * Verify results
     */
    /* Make sure remove helper did not fail */
    if(OPA_load_int(&int_helper_data.num_failed) > 0)
        TEST_ERROR;

    /* Verify all tasks were either canceled or executed */
    if(OPA_load_int(&int_helper_data.num_cancels)
            + OPA_load_int(&shared_task_data.ncalls) != CREATE_REMOVE_NTASKS)
        TEST_ERROR;

    /* Loop over all tasks, making sure completed tasks are marked completed,
     * canceled tasks are marked canceled, tasks with parents were executed
     * after their parents, and tasks without parents follow canceled tasks */
    total_ncalls = 0;
    last_run_order = -1;
    last_status = AXE_TASK_DONE;
    for(i = 0; i < CREATE_REMOVE_NTASKS; i++) {
        if(task_data[i].failed != 0)
            TEST_ERROR;
        if(AXEget_status(helper_data->engine, task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(task_data[i].run_order == -1) {
            if(status != AXE_TASK_CANCELED)
                TEST_ERROR;
        } /* end if */
        else {
            if(status != AXE_TASK_DONE)
                TEST_ERROR;
            if(task_data[i].cond_signal_sent == 1) {
                if(last_status != AXE_TASK_CANCELED)
                    TEST_ERROR;
            } /* end if */
            else {
                if(last_status != AXE_TASK_DONE)
                    TEST_ERROR;
                if(task_data[i].run_order <= last_run_order)
                    TEST_ERROR;
            } /* end else */
            total_ncalls++;
        } /* end else */
        last_run_order = task_data[i].run_order;
        last_status = status;
    } /* end for */
    if(total_ncalls != OPA_load_int(&shared_task_data.ncalls))
        TEST_ERROR;

    /*
     * Close
     */
    for(i = 0; i < CREATE_REMOVE_NTASKS; i++)
        if(AXEfinish(helper_data->engine, task[i]) != AXE_SUCCEED)
            TEST_ERROR;
    if(AXEfinish(helper_data->engine, remove_helper_task) != AXE_SUCCEED)
        TEST_ERROR;
    free(task_data);
    free(task);

    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    OPA_incr_int(&helper_data->nfailed);
assert(0);
    return;
} /* end test_create_remove_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_create_remove_all_helper
 *
 * Purpose:     Runs 2 threads simultaneously (within the engine), one of
 *              which adds new tasks and the other which repeatedly calls
 *              AXE_remove_all().
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              March 26, 2013
 *
 *-------------------------------------------------------------------------
 */
/* Data shared between all remove helper tasks */
typedef struct create_remove_all_shared_t {
    int num_creates;            /* Number of tasks created */
    pthread_cond_t create_cond; /* Condition variable signaled whenever a task is created */
    pthread_mutex_t create_cond_mutex; /* Mutex associated with create_cond */
    OPA_int_t num_failed;       /* Number of helper threads failed */
    _Bool shutdown;             /* Whether to shut down */
} create_remove_all_shared_t;


/* Secondary helper function.  Constantly tries to remove */
void
create_remove_all_remove_helper(AXE_engine_t engine,
    size_t num_necessary_parents, AXE_task_t necessary_parents[],
    size_t num_sufficient_parents, AXE_task_t sufficient_parents[],
    void *_task_data)
{
    create_remove_all_shared_t *helper_data = (create_remove_all_shared_t *)_task_data;
    int num_removes = 0;

    assert(helper_data);

    /* Loop until told to shut dbown */
    while(!helper_data->shutdown) {
        /* Cancel all tasks */
        if(AXEremove_all(engine, NULL) != AXE_SUCCEED)
            OPA_incr_int(&helper_data->num_failed);
        num_removes++;

        /* Wait on condition if we have done too many removes relative to the
         * number of tasks.  Stops this test from running unreasonably long. */
        if(0 != pthread_mutex_lock(&helper_data->create_cond_mutex))
            OPA_incr_int(&helper_data->num_failed);
        while((num_removes > (2 * helper_data->num_creates))
                && !helper_data->shutdown)
            if(0 != pthread_cond_wait(&helper_data->create_cond, &helper_data->create_cond_mutex))
                OPA_incr_int(&helper_data->num_failed);
        if(0 != pthread_mutex_unlock(&helper_data->create_cond_mutex))
            OPA_incr_int(&helper_data->num_failed);
    } /* end while */

    /* Make sure there are no parents */
    if((num_necessary_parents != 0) || (num_sufficient_parents != 0))
        OPA_incr_int(&helper_data->num_failed);

    return;
} /* end create_remove_all_remove_helper() */


/* Main test helper function */
void
test_create_remove_all_helper(AXE_engine_t _engine,
    size_t num_necessary_parents, AXE_task_t necessary_parents[],
    size_t num_sufficient_parents, AXE_task_t sufficient_parents[],
    void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_engine_t engine;
    AXE_engine_attr_t engine_attr;
    _Bool engine_init = FALSE;
    create_remove_all_shared_t int_helper_data;
    basic_task_t *task_data = NULL;
    basic_task_shared_t shared_task_data;
    AXE_task_t parent_task;
    AXE_task_t task;
    int total_ncalls;
    int last_run_order;
    AXE_error_t ret;
    int i;

    /* Reserve threads for engine */
    MAX_NTHREADS_RESERVE(helper_data->num_threads, TEST_ERROR);

    /* Initialize engine attribute */
    if(AXEengine_attr_init(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create AXE engine */
    if(AXEset_num_threads(&engine_attr, helper_data->num_threads) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_engine(&engine, &engine_attr) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = TRUE;

    /* Initialize int_helper_data */
    int_helper_data.num_creates = 0;
    if(0 != pthread_cond_init(&int_helper_data.create_cond, NULL))
        TEST_ERROR;
    if(0 != pthread_mutex_init(&int_helper_data.create_cond_mutex, NULL))
        TEST_ERROR;
    OPA_store_int(&int_helper_data.num_failed, 0);
    int_helper_data.shutdown = FALSE;

    /* Allocate task_data */
    if(NULL == (task_data = (basic_task_t *)malloc(CREATE_REMOVE_ALL_NTASKS * sizeof(basic_task_t))))
        TEST_ERROR;

    /* Initialize shared_task_data */
    shared_task_data.max_ncalls = CREATE_REMOVE_ALL_NTASKS;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Launch remove helper */
    if(AXEgenerate_task_id(engine, &task) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, task, 0, NULL, 0, NULL, create_remove_all_remove_helper, &int_helper_data, NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(engine, task) != AXE_SUCCEED)
        TEST_ERROR;

    /* Launch CREATE_REMOVE_ALL_NTASKS tasks */
    for(i = 0; i < CREATE_REMOVE_ALL_NTASKS; i++) {
        /* Initialize task data */
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].run_order = -1;
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
        task_data[i].cond_signal_sent = 0;

        /* Try to launch task with parent.  If it fails (due to parent being
         * canceled), launch without parent.  Do not use parent for first task.
         * Use cond_signal_sent field to keep track of which tasks have no
         * parents (1 == no parent). */
        if(i == 0)
            /* This is the first tast: mark ret as AXE_FAIL so we create a task
             * without a parent below */
            ret = AXE_FAIL;
        else {
            /* Create task with parent.  May fail. */
            if(AXEgenerate_task_id(engine, &task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEbegin_try() != AXE_SUCCEED)
                TEST_ERROR;
            ret = AXEcreate_task(engine, task, 1, &parent_task, 0, NULL, basic_task_worker, &task_data[i], NULL);
            if(AXEend_try() != AXE_SUCCEED)
                TEST_ERROR;

            /* Close parent task */
            if(AXEfinish(engine, parent_task) != AXE_SUCCEED)
                TEST_ERROR;
        } /* end else */
        if(ret != AXE_SUCCEED) {
            /* Mark task as having no parent */
            task_data[i].cond_signal_sent = 1;

            /* Create task without parent */
            if(AXEgenerate_task_id(engine, &task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(engine, task, 0, NULL, 0, NULL, basic_task_worker, &task_data[i], NULL) != AXE_SUCCEED)
                TEST_ERROR;
        } /* end if */

        /* Send signal that task was created */
        if(0 != pthread_mutex_lock(&int_helper_data.create_cond_mutex))
            TEST_ERROR;
        int_helper_data.num_creates++;
        if(0 != pthread_cond_signal(&int_helper_data.create_cond))
            TEST_ERROR;
        if(0 != pthread_mutex_unlock(&int_helper_data.create_cond_mutex))
            TEST_ERROR;

        /* Update parent_task */
        parent_task = task;
    } /* end for */

    /* Send signal to shut down remove helper */
    if(0 != pthread_mutex_lock(&int_helper_data.create_cond_mutex))
        TEST_ERROR;
    int_helper_data.shutdown = TRUE;
    if(0 != pthread_cond_signal(&int_helper_data.create_cond))
        TEST_ERROR;
    if(0 != pthread_mutex_unlock(&int_helper_data.create_cond_mutex))
        TEST_ERROR;


    /* Close task */
    if(AXEfinish(engine, task) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks and remove helper to complete.  Do this by terminating the
     * engine with wait_all set to TRUE. */
    AXE_test_exclude_close_on(engine);
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Release threads used by engine */
    MAX_NTHREADS_RELEASE(helper_data->num_threads, TEST_ERROR);

    /* Destroy engine attribute */
    if(AXEengine_attr_destroy(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    engine_init = FALSE;

    /*
     * Verify results
     */
    /* Make sure remove helper did not fail */
    if(OPA_load_int(&int_helper_data.num_failed) > 0)
        TEST_ERROR;

    /* Loop over all tasks, making sure none failed, tasks with parents were
     * executed after their parents, and tasks without parents follow canceled
     * tasks */
    total_ncalls = 0;
    last_run_order = -1;
    for(i = 0; i < CREATE_REMOVE_ALL_NTASKS; i++) {
        if(task_data[i].failed != 0)
            TEST_ERROR;
        if(task_data[i].run_order != -1) {
            if(task_data[i].cond_signal_sent == 1) {
                if(last_run_order != -1)
                    TEST_ERROR;
            } /* end if */
            else
                if(task_data[i].run_order <= last_run_order)
                    TEST_ERROR;
            total_ncalls++;
        } /* end else */
        last_run_order = task_data[i].run_order;
    } /* end for */
    if(total_ncalls != OPA_load_int(&shared_task_data.ncalls))
        TEST_ERROR;

    /*
     * Close
     */
    free(task_data);
    if(0 != pthread_cond_destroy(&int_helper_data.create_cond))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&int_helper_data.create_cond_mutex))
        TEST_ERROR;

    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    if(engine_init) {
        (void)AXEterminate_engine(engine, FALSE);
        MAX_NTHREADS_RELEASE(helper_data->num_threads, );
        (void)AXEengine_attr_destroy(&engine_attr);
    } /* end if */

    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_create_remove_all_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_pileup_helper
 *
 * Purpose:     Repeatedly launches tasks which call AXEcreate_task(),
 *              AXEcreate_barrier_task(), AXEremove(), or AXEremove_all(),
 *              then return.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              March 26, 2013
 *
 *-------------------------------------------------------------------------
 */
/* Data shared between all remove helper tasks */
typedef struct pileup_shared_t {
    basic_task_t *task_data;    /* Task data array for task to create */
    OPA_int_t task_data_i;      /* Index into task_data for next task */
    OPA_ptr_t rem_task;         /* Pointer to task to remove */
    OPA_int_t num_failed;       /* Number of helper threads failed */
} pileup_shared_t;


/* Secondary helper function.  Creates a task and returns. */
void
pileup_create_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_task_data)
{
    pileup_shared_t *helper_data = (pileup_shared_t *)_task_data;
    AXE_task_t *task = NULL;

    assert(helper_data);

    /* Allocate space for task */
    if(NULL == (task = (AXE_task_t *)malloc(sizeof(AXE_task_t))))
        TEST_ERROR;

    /* Create task */
    if(AXEgenerate_task_id(engine, task) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, *task, 0, NULL, 0, NULL, basic_task_worker, &helper_data->task_data[OPA_fetch_and_incr_int(&helper_data->task_data_i)], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Add task to shared struct, finish and free previous task in shared struct
     */
    task = OPA_swap_ptr(&helper_data->rem_task, task);
    if(task) {
        if(AXEfinish(engine, *task) != AXE_SUCCEED)
            OPA_incr_int(&helper_data->num_failed);
        free(task);
    } /* end if */

    /* Make sure there are no parents */
    if((num_necessary_parents != 0) || (num_sufficient_parents != 0))
        OPA_incr_int(&helper_data->num_failed);

    return;

error:
    OPA_incr_int(&helper_data->num_failed);
    return;
} /* end pileup_create_helper() */


/* Secondary helper function.  Creates a barrier task and returns. */
void
pileup_create_barrier_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_task_data)
{
    pileup_shared_t *helper_data = (pileup_shared_t *)_task_data;
    AXE_task_t *task = NULL;

    assert(helper_data);

    /* Allocate space for task */
    if(NULL == (task = (AXE_task_t *)malloc(sizeof(AXE_task_t))))
        TEST_ERROR;

    /* Create task */
    if(AXEgenerate_task_id(engine, task) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_barrier_task(engine, *task, basic_task_worker, &helper_data->task_data[OPA_fetch_and_incr_int(&helper_data->task_data_i)], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Add task to shared struct, finish and free previous task in shared struct
     */
    task = OPA_swap_ptr(&helper_data->rem_task, task);
    if(task) {
        if(AXEfinish(engine, *task) != AXE_SUCCEED)
            OPA_incr_int(&helper_data->num_failed);
        free(task);
    } /* end if */

    /* Make sure there are no parents */
    if((num_necessary_parents != 0) || (num_sufficient_parents != 0))
        OPA_incr_int(&helper_data->num_failed);

    return;

error:
    OPA_incr_int(&helper_data->num_failed);
    return;
} /* end pileup_create_barrier_helper() */


/* Secondary helper function.  Removes a task and returns. */
void
pileup_remove_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_task_data)
{
    pileup_shared_t *helper_data = (pileup_shared_t *)_task_data;
    AXE_task_t *rem_task = NULL;

    assert(helper_data);

    /* Load task to remove, swapping shared pointer with NULL so the task
     * doesn't get freed out from under us */
    rem_task = (AXE_task_t *)OPA_swap_ptr(&helper_data->rem_task, NULL);

    if(rem_task) {
        /* Cancel task - note this can fail if the task has children (i.e.
         * barrier tasks) */
        if(AXEbegin_try() != AXE_SUCCEED)
            OPA_incr_int(&helper_data->num_failed);
        (void)AXEremove(engine, *rem_task, NULL);
        if(AXEend_try() != AXE_SUCCEED)
            OPA_incr_int(&helper_data->num_failed);

        /* Finish and free rem_task */
        if(AXEfinish(engine, *rem_task) != AXE_SUCCEED)
            OPA_incr_int(&helper_data->num_failed);
        free(rem_task);
    } /* end if */

    /* Make sure there are no parents */
    if((num_necessary_parents != 0) || (num_sufficient_parents != 0))
        OPA_incr_int(&helper_data->num_failed);

    return;
} /* end pileup_remove_helper() */


/* Secondary helper function.  Removes all tasks and returns. */
void
pileup_remove_all_helper(AXE_engine_t engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_task_data)
{
    pileup_shared_t *helper_data = (pileup_shared_t *)_task_data;

    assert(helper_data);

    /* Cancel all tasks */
    if(AXEremove_all(engine, NULL) != AXE_SUCCEED)
        OPA_incr_int(&helper_data->num_failed);

    /* Make sure there are no parents */
    if((num_necessary_parents != 0) || (num_sufficient_parents != 0))
        OPA_incr_int(&helper_data->num_failed);

    return;
} /* end pileup_remove_all_helper() */


/* Main test helper function */
void
test_pileup_helper(AXE_engine_t _engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_engine_t engine;
    AXE_engine_attr_t engine_attr;
    _Bool engine_init = FALSE;;
    pileup_shared_t int_helper_data;
    basic_task_shared_t shared_task_data;
    AXE_task_t tmp_task;
    int total_ncalls;
    int i;

    /* Reserve threads for engine */
    MAX_NTHREADS_RESERVE(helper_data->num_threads, TEST_ERROR);

    /* Initialize engine attribute */
    if(AXEengine_attr_init(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create AXE engine */
    if(AXEset_num_threads(&engine_attr, helper_data->num_threads) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_engine(&engine, &engine_attr) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = TRUE;

    /* Initialize int_helper_data */
    if(NULL == (int_helper_data.task_data = (basic_task_t *)malloc(PILEUP_NTASKS * sizeof(basic_task_t))))
        TEST_ERROR;
    OPA_store_int(&int_helper_data.task_data_i, 0);
    OPA_store_ptr(&int_helper_data.rem_task, NULL);
    OPA_store_int(&int_helper_data.num_failed, 0);

    /* Initialize shared_task_data */
    shared_task_data.max_ncalls = PILEUP_NTASKS;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task_data */
    for(i = 0; i < PILEUP_NTASKS; i++) {
        int_helper_data.task_data[i].shared = &shared_task_data;
        int_helper_data.task_data[i].failed = 0;
        int_helper_data.task_data[i].run_order = -1;
        int_helper_data.task_data[i].mutex = NULL;
        int_helper_data.task_data[i].cond = NULL;
        int_helper_data.task_data[i].cond_mutex = NULL;
    } /* end for */

    /* Launch CREATE_REMOVE_ALL_NTASKS tasks */
    for(i = 0; i < (PILEUP_NTASKS / 2); i++) {
        /* Launch create helper */
        if(AXEgenerate_task_id(engine, &tmp_task) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(engine, tmp_task, 0, NULL, 0, NULL, pileup_create_helper, &int_helper_data, NULL) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(engine, tmp_task) != AXE_SUCCEED)
            TEST_ERROR;

        /* Launch create barrier helper */
        if(AXEgenerate_task_id(engine, &tmp_task) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(engine, tmp_task, 0, NULL, 0, NULL, pileup_create_barrier_helper, &int_helper_data, NULL) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(engine, tmp_task) != AXE_SUCCEED)
            TEST_ERROR;

        /* Launch remove helper */
        if(AXEgenerate_task_id(engine, &tmp_task) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(engine, tmp_task, 0, NULL, 0, NULL, pileup_remove_helper, &int_helper_data, NULL) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(engine, tmp_task) != AXE_SUCCEED)
            TEST_ERROR;

        /* Launch remove all helper */
        if(AXEgenerate_task_id(engine, &tmp_task) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEcreate_task(engine, tmp_task, 0, NULL, 0, NULL, pileup_remove_all_helper, &int_helper_data, NULL) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(engine, tmp_task) != AXE_SUCCEED)
            TEST_ERROR;
    } /* end for */

    /* Wait for tasks and remove helper to complete.  Do this by terminating the
     * engine with wait_all set to TRUE.  Note that we cannot use exclude_close,
     * because the last rem_task is left open.  We cannot close it because there
     * is no way to wait for all the tasks to complete (a barrier task could
     * miss tasks that are created by pileup_create_helper() or
     * pileup_create_barrier_helper()). */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Release threads used by engine */
    MAX_NTHREADS_RELEASE(helper_data->num_threads, TEST_ERROR);

    /* Destroy engine attribute */
    if(AXEengine_attr_destroy(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    engine_init = FALSE;

    /*
     * Verify results
     */
    /* Make sure no helpers failed */
    if(OPA_load_int(&int_helper_data.num_failed) > 0)
        TEST_ERROR;

    /* Loop over all tasks, making sure none failed */
    total_ncalls = 0;
    for(i = 0; i < PILEUP_NTASKS; i++) {
        if(int_helper_data.task_data[i].failed != 0)
            TEST_ERROR;
        if(int_helper_data.task_data[i].run_order != -1)
            total_ncalls++;
    } /* end for */
    if(total_ncalls != OPA_load_int(&shared_task_data.ncalls))
        TEST_ERROR;

    /*
     * Close
     */
    /* Free rem_task */
    if(OPA_load_ptr(&int_helper_data.rem_task))
        free(OPA_load_ptr(&int_helper_data.rem_task));

    free(int_helper_data.task_data);

    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    if(engine_init) {
        (void)AXEterminate_engine(engine, FALSE);
        MAX_NTHREADS_RELEASE(helper_data->num_threads, );
        (void)AXEengine_attr_destroy(&engine_attr);
    } /* end if */

    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_pileup_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_id_helper
 *
 * Purpose:     Simple test for task id functionality.  Only tests serial
 *              functionality, does not test threadsafety of id table,
 *              threrefore only needs to be run once.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              April 23, 2013
 *
 *-------------------------------------------------------------------------
 */
void
test_id_helper(AXE_engine_t _engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_engine_t engine;
    AXE_engine_attr_t engine_attr;
    AXE_task_t task[7];
    int i;

    /* Initialize engine attribute */
    if(AXEengine_attr_init(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Set number of threads to 1 */
    if(AXEset_num_threads(&engine_attr, 1) != AXE_SUCCEED)
        TEST_ERROR;

    /* Set id range to 2-4 */
    if(AXEset_id_range(&engine_attr, 2, 4) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 1: Only automatically generated ids.  Verify that id exhaustion is
     * handled gracefully, verify that ids can be recycled, Verify that ids used
     * to create a task cannot be released.
     */
    /* Create AXE engine */
    if(AXEcreate_engine(&engine, &engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Generate 3 ids, verify that they are 2, 3, and 4 */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(task[0] != 2)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(task[1] != 3)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(task[2] != 4)
        TEST_ERROR;

    /* Try to generate another id.  Should fail. */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEgenerate_task_id(engine, &task[3]) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Release task[1] (id 3) */
    if(AXErelease_task_id(engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Generate another id.  Should take id 3. */
    if(AXEgenerate_task_id(engine, &task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(task[1] != 3)
        TEST_ERROR;

    /* Associate a task with task[1] (id 3) */
    if(AXEcreate_task(engine, task[1], 0, NULL, 0, NULL, NULL, NULL, NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Try to release task[1].  Should fail. */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXErelease_task_id(engine, task[1]) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Finish task[1] */
    if(AXEfinish(engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Terminate engine */
    AXE_test_exclude_close_on(engine);
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 2: Only manually generated ids.  Verify that ids can be manually
     * given above, below, and within the range set by AXEset_id_range().
     */
    /* Create AXE engine */
    if(AXEcreate_engine(&engine, &engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create tasks with ids 1-5 */
    for(i = 0; i < 5; i++) {
        task[i] = i + 1;
        if(AXEcreate_task(engine, task[i], 0, NULL, 0, NULL, NULL, NULL, NULL) != AXE_SUCCEED)
            TEST_ERROR;
    } /* end for */

    /* Try to release tasks.  Should fail. */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    for(i = 0; i < 5; i++)
        if(AXErelease_task_id(engine, task[i]) != AXE_FAIL)
            TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Finish tasks */
    if(AXEfinish_all(engine, 5, task) != AXE_SUCCEED)
        TEST_ERROR;

    /* Terminate engine */
    AXE_test_exclude_close_on(engine);
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 3: Mix automatic and manual id generation.  Make sure manual ids
     * outside the range do not affect automatic generation.  Make sure manual
     * ids inside the range are taken into account.
     */
    /* Set id range to 2-8 */
    if(AXEset_id_range(&engine_attr, 2, 8) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create AXE engine */
    if(AXEcreate_engine(&engine, &engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Generate and release an automatic id */
    if(AXEgenerate_task_id(engine, &task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(task[0] != 2)
        TEST_ERROR;
    if(AXErelease_task_id(engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create manual tasks above and below id range */
    task[0] = 1;
    if(AXEcreate_task(engine, task[0], 0, NULL, 0, NULL, NULL, NULL, NULL) != AXE_SUCCEED)
        TEST_ERROR;
    task[1] = 9;
    if(AXEcreate_task(engine, task[1], 0, NULL, 0, NULL, NULL, NULL, NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Generate another automatic id.  Should be 3. */
    if(AXEgenerate_task_id(engine, &task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(task[2] != 3)
        TEST_ERROR;

    /* Create a manual task with id 5 */
    task[3] = 5;
    if(AXEcreate_task(engine, task[3], 0, NULL, 0, NULL, NULL, NULL, NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Generate another automatic id.  Should be 6. */
    if(AXEgenerate_task_id(engine, &task[4]) != AXE_SUCCEED)
        TEST_ERROR;
    if(task[4] != 6)
        TEST_ERROR;

    /* Create a manual task with id 8 */
    task[5] = 8;
    if(AXEcreate_task(engine, task[5], 0, NULL, 0, NULL, NULL, NULL, NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Generate another automatic id.  Should be 2. */
    if(AXEgenerate_task_id(engine, &task[6]) != AXE_SUCCEED)
        TEST_ERROR;
    if(task[6] != 2)
        TEST_ERROR;

    /* Finish created tasks */
    if(AXEfinish(engine, task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(engine, task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(engine, task[3]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(engine, task[5]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Terminate engine */
    AXE_test_exclude_close_on(engine);
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Close
     */
    /* Destroy engine attribute */
    if(AXEengine_attr_destroy(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    (void)AXEterminate_engine(engine, FALSE);

    (void)AXEengine_attr_destroy(&engine_attr);

    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_id_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_attr_helper
 *
 * Purpose:     Simple test for engine attribute functionality.  Only
 *              tests serial functionality, does not test threadsafety of
 *              anything, threrefore only needs to be run once.
 *
 * Return:      void
 *
 * Programmer:  Neil Fortner
 *              April 24, 2013
 *
 *-------------------------------------------------------------------------
 */
void
test_attr_helper(AXE_engine_t _engine, size_t num_necessary_parents,
    AXE_task_t necessary_parents[], size_t num_sufficient_parents,
    AXE_task_t sufficient_parents[], void *_helper_data)
{
    test_helper_t *helper_data = (test_helper_t *)_helper_data;
    AXE_engine_t engine;
    _Bool engine_init = FALSE;
    AXE_engine_attr_t engine_attr;
    size_t num_threads;
    AXE_task_t min_id;
    AXE_task_t max_id;
    size_t num_buckets;
    size_t num_mutexes;
    AXE_task_t task;

    /* Initialize engine attribute */
    if(AXEengine_attr_init(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 1: num_threads
     */
    /* Get number of threads, verify it is the default (8) */
    if(AXEget_num_threads(&engine_attr, &num_threads) != AXE_SUCCEED)
        TEST_ERROR;
    if(num_threads != 8)
        TEST_ERROR;

    /* Change number of threads to 9 */
    if(AXEset_num_threads(&engine_attr, 9) != AXE_SUCCEED)
        TEST_ERROR;

    /* Get number of threads, verify it is 9 */
    if(AXEget_num_threads(&engine_attr, &num_threads) != AXE_SUCCEED)
        TEST_ERROR;
    if(num_threads != 9)
        TEST_ERROR;

    /* Try to change number of threads to 0 (should fail) */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEset_num_threads(&engine_attr, 0) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Get number of threads, verify it is 9 */
    if(AXEget_num_threads(&engine_attr, &num_threads) != AXE_SUCCEED)
        TEST_ERROR;
    if(num_threads != 9)
        TEST_ERROR;


    /*
     * Test 2: id range
     */
    /* Get id range, verify it is the default (0-UINT64_MAX) */
    if(AXEget_id_range(&engine_attr, &min_id, &max_id) != AXE_SUCCEED)
        TEST_ERROR;
    if(min_id != 0)
        TEST_ERROR;
    if(max_id != UINT64_MAX)
        TEST_ERROR;

    /* Change range to 3-42 */
    if(AXEset_id_range(&engine_attr, 3, 42) != AXE_SUCCEED)
        TEST_ERROR;

    /* Get id range verify it is 3-42 */
    if(AXEget_id_range(&engine_attr, &min_id, &max_id) != AXE_SUCCEED)
        TEST_ERROR;
    if(min_id != 3)
        TEST_ERROR;
    if(max_id != 42)
        TEST_ERROR;

    /* Change range to 11-11 */
    if(AXEset_id_range(&engine_attr, 11, 11) != AXE_SUCCEED)
        TEST_ERROR;

    /* Get id range verify it is 11-11 */
    if(AXEget_id_range(&engine_attr, &min_id, &max_id) != AXE_SUCCEED)
        TEST_ERROR;
    if(min_id != 11)
        TEST_ERROR;
    if(max_id != 11)
        TEST_ERROR;

    /* Change range to 11-10 (should fail) */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEset_id_range(&engine_attr, 11, 10) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Get id range verify it is 11-11 */
    if(AXEget_id_range(&engine_attr, &min_id, &max_id) != AXE_SUCCEED)
        TEST_ERROR;
    if(min_id != 11)
        TEST_ERROR;
    if(max_id != 11)
        TEST_ERROR;


    /*
     * Test 3: num_buckets
     */
    /* Get number of buckets, verify it is the default (10007) */
    if(AXEget_num_id_buckets(&engine_attr, &num_buckets) != AXE_SUCCEED)
        TEST_ERROR;
    if(num_buckets != 10007)
        TEST_ERROR;

    /* Change number of buckets to 8419 */
    if(AXEset_num_id_buckets(&engine_attr, 8419) != AXE_SUCCEED)
        TEST_ERROR;

    /* Get number of buckets, verify it is 8419 */
    if(AXEget_num_id_buckets(&engine_attr, &num_buckets) != AXE_SUCCEED)
        TEST_ERROR;
    if(num_buckets != 8419)
        TEST_ERROR;

    /* Try to change number of buckets to 0 (should fail) */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEset_num_id_buckets(&engine_attr, 0) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Get number of buckets, verify it is 8419 */
    if(AXEget_num_id_buckets(&engine_attr, &num_buckets) != AXE_SUCCEED)
        TEST_ERROR;
    if(num_buckets != 8419)
        TEST_ERROR;


    /*
     * Test 4: num_mutexes
     */
    /* Get number of mutexes, verify it is the default (503) */
    if(AXEget_num_id_mutexes(&engine_attr, &num_mutexes) != AXE_SUCCEED)
        TEST_ERROR;
    if(num_mutexes != 503)
        TEST_ERROR;

    /* Change number of mutexes to 521 */
    if(AXEset_num_id_mutexes(&engine_attr, 521) != AXE_SUCCEED)
        TEST_ERROR;

    /* Get number of mutexes, verify it is 521 */
    if(AXEget_num_id_mutexes(&engine_attr, &num_mutexes) != AXE_SUCCEED)
        TEST_ERROR;
    if(num_mutexes != 521)
        TEST_ERROR;

    /* Try to change number of mutexes to 0 (should fail) */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEset_num_id_mutexes(&engine_attr, 0) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Get number of mutexes, verify it is 521 */
    if(AXEget_num_id_mutexes(&engine_attr, &num_mutexes) != AXE_SUCCEED)
        TEST_ERROR;
    if(num_mutexes != 521)
        TEST_ERROR;


    /*
     * Test 5: Create engine
     */
    /* Create AXE engine using the attribute */
    if(AXEcreate_engine(&engine, &engine_attr) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = TRUE;

    /* Create a task using the engine */
    if(AXEgenerate_task_id(engine, &task) != AXE_SUCCEED)
        TEST_ERROR;
    if(task != 11)
        TEST_ERROR;
    if(AXEcreate_task(engine, task, 0, NULL, 0, NULL, NULL, NULL, NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Finish task */
    if(AXEfinish(engine, task) != AXE_SUCCEED)
        TEST_ERROR;

    /* Terminate engine */
    AXE_test_exclude_close_on(engine);
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = FALSE;


    /*
     * Test 5: Create engine with NULL attribute
     */
    /* Create AXE engine using NULL attribute */
    if(AXEcreate_engine(&engine, NULL) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = TRUE;

    /* Create a task using the engine */
    if(AXEgenerate_task_id(engine, &task) != AXE_SUCCEED)
        TEST_ERROR;
    if(task != 0)
        TEST_ERROR;
    if(AXEcreate_task(engine, task, 0, NULL, 0, NULL, NULL, NULL, NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Finish task */
    if(AXEfinish(engine, task) != AXE_SUCCEED)
        TEST_ERROR;

    /* Terminate engine */
    AXE_test_exclude_close_on(engine);
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = FALSE;


    /*
     * Close
     */
    /* Destroy engine attribute */
    if(AXEengine_attr_destroy(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    OPA_incr_int(&helper_data->ncomplete);

    return;

error:
    if(engine_init)
        (void)AXEterminate_engine(engine, FALSE);

    (void)AXEengine_attr_destroy(&engine_attr);

    OPA_incr_int(&helper_data->nfailed);

    return;
} /* end test_attr_helper() */


/*-------------------------------------------------------------------------
 * Function:    test_serial
 *
 * Purpose:     Runs the test supplied in the helper argument.  Uses
 *              num_threads threads, runs the test niter times, creates an
 *              engine if create_engine is set to TRUE, and uses test_name
 *              to print a message.
 *
 * Return:      Success: 0
 *              Failure: 1
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
int
test_serial(AXE_task_op_t helper, size_t num_threads, size_t niter,
    _Bool create_engine, char *test_name)
{
    test_helper_t helper_data;
    AXE_engine_attr_t engine_attr;
    size_t i;

    TESTING(test_name);

#ifdef AXE_DEBUG_PERF
    OPA_store_int(&AXE_debug_nspins_add, 0);
    OPA_store_int(&AXE_debug_nspins_finish, 0);
    OPA_store_int(&AXE_debug_nadds, 0);
#endif /* AXE_DEBUG_PERF */

    helper_data.engine = NULL;
    
    /* Initialize engine attribute */
    if(AXEengine_attr_init(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Set number of threads on engine attribute */
    if(AXEset_num_threads(&engine_attr, num_threads) != AXE_SUCCEED)
        TEST_ERROR;

    /* Perform niter iterations of the test */
    for(i = 0; i < niter; i++) {
        /* Initialize helper data struct */
        helper_data.num_threads = num_threads;
        OPA_store_int(&helper_data.nfailed, 0);
        OPA_store_int(&helper_data.ncomplete, 0);
        helper_data.parallel_mutex = NULL;

        /* Create AXE engine if requested.  No need to worry about number of
         * threads because it has already been cleared by main and no other
         * tests will be run at the same time */
        if(create_engine) {
            if(AXEcreate_engine(&helper_data.engine, &engine_attr) != AXE_SUCCEED)
                TEST_ERROR;
        } /* end if */
        else
            helper_data.engine = NULL;

        /* Launch test helper */
        helper(NULL, 0, NULL, 0, NULL, &helper_data);

        /* Check for error */
        if(OPA_load_int(&helper_data.nfailed) != 0)
            goto error;
        if(OPA_load_int(&helper_data.ncomplete) != 1)
            TEST_ERROR;

        /* Terminate engine */
        if(create_engine) {
            AXE_test_exclude_close_on(helper_data.engine);
            if(AXEterminate_engine(helper_data.engine, TRUE) != AXE_SUCCEED)
                TEST_ERROR;
            helper_data.engine = NULL;
        } /* end if */
    } /* end for */

    /* Destroy engine attribute */
    if(AXEengine_attr_destroy(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    PASSED();

#ifdef AXE_DEBUG_PERF
    printf(" Adds: %d, Spins in add_common(): %d, Spins in finish(): %d\n", OPA_load_int(&AXE_debug_nadds), OPA_load_int(&AXE_debug_nspins_add), OPA_load_int(&AXE_debug_nspins_finish));
#endif /* AXE_DEBUG_PERF */

    return 0;

error:
    if(helper_data.engine) {
        (void)AXEterminate_engine(helper_data.engine, FALSE);
        MAX_NTHREADS_RELEASE(num_threads, );
    } /* end if */

    (void)AXEengine_attr_destroy(&engine_attr);

    return 1;
} /* end test_serial() */


/*-------------------------------------------------------------------------
 * Function:    test_parallel
 *
 * Purpose:     Runs all tests concurrently, each with approximately as
 *              many iterations as it would normally.  All tests that can
 *              share an engine, others create their own.  Tests are
 *              launched by AXE, using meta_engine.
 *
 * Return:      Success: 0
 *              Failure: 1
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
int
test_parallel(size_t num_threads_meta, size_t num_threads_int, size_t niter)
{
    AXE_engine_t meta_engine;
    test_helper_t helper_data;
    AXE_engine_attr_t engine_attr;
    pthread_mutex_t parallel_mutex;
    _Bool meta_engine_init = FALSE;
    AXE_task_t tmp_task;
    size_t simple_i = 0;
    size_t necessary_i = 0;
    size_t sufficient_i = 0;
    size_t barrier_i = 0;
    size_t get_op_data_i = 0;
    size_t finish_all_i = 0;
    size_t free_op_data_i = 0;
    size_t remove_i = 0;
    size_t remove_all_i = 0;
    size_t terminate_engine_i = 0;
    size_t fractal_i = 0;
    size_t fractal_nodep_i = 0;
    size_t create_remove_i = 0;
    size_t create_remove_all_i = 0;
    size_t pileup_i = 0;
    size_t num_threads_i = 0;
    size_t i;

    TESTING("parallel execution of all tests");

#ifdef AXE_DEBUG_PERF
    OPA_store_int(&AXE_debug_nspins_add, 0);
    OPA_store_int(&AXE_debug_nspins_finish, 0);
    OPA_store_int(&AXE_debug_nadds, 0);
#endif /* AXE_DEBUG_PERF */

    /* Initialize parallel mutex.  Only used to prevent simultaneous execution
     * of tests that require a minimum number of threads in the shared engine.
     * The majority of tests do not take the mutex. */
    if(0 != pthread_mutex_init(&parallel_mutex, NULL))
        TEST_ERROR;

    /* Initialize helper data struct */
    helper_data.num_threads = num_threads_int;
    OPA_store_int(&helper_data.nfailed, 0);
    OPA_store_int(&helper_data.ncomplete, 0);
    helper_data.parallel_mutex = &parallel_mutex;

    /* Reserve threads for internal engine */
    MAX_NTHREADS_RESERVE(num_threads_int, TEST_ERROR);

    /* Initialize engine attribute */
    if(AXEengine_attr_init(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create internal engine for use by helper tasks */
    if(AXEset_num_threads(&engine_attr, num_threads_int) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_engine(&helper_data.engine, &engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    /* Reserve threads for meta engine */
    MAX_NTHREADS_RESERVE(num_threads_meta, TEST_ERROR);

    /* Create meta engine to assist in spawning helper tasks */
    if(AXEset_num_threads(&engine_attr, num_threads_meta) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_engine(&meta_engine, &engine_attr) != AXE_SUCCEED)
        TEST_ERROR;
    meta_engine_init = TRUE;

    /* Make PARALLEL_NITER passes, adding each helper to the meta engine a
     * number of times equal to its NITER macro */
    for(i = 0; i < niter; i++) {
        /* Launch simple test */
        if(i >= simple_i * PARALLEL_NITER / SIMPLE_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_simple_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            simple_i++;
        } /* end if */

        /* Launch necessary test */
        if(i >= necessary_i * PARALLEL_NITER / NECESSARY_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_necessary_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            necessary_i++;
        } /* end if */

        /* Launch sufficient test */
        if(i >= sufficient_i * PARALLEL_NITER / SUFFICIENT_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_sufficient_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            sufficient_i++;
        } /* end if */

        /* Launch barrier test */
        if(i >= barrier_i * PARALLEL_NITER / BARRIER_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_barrier_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            barrier_i++;
        } /* end if */

        /* Launch get_op_data test */
        if(i >= get_op_data_i * PARALLEL_NITER / GET_OP_DATA_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_get_op_data_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            get_op_data_i++;
        } /* end if */

        /* Launch finish_all test */
        if(i >= finish_all_i * PARALLEL_NITER / FINISH_ALL_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_finish_all_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            finish_all_i++;
        } /* end if */

        /* Launch free_op_data test */
        if(i >= free_op_data_i * PARALLEL_NITER / FREE_OP_DATA_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_free_op_data_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            free_op_data_i++;
        } /* end if */

        /* Launch remove test */
        if(i >= remove_i * PARALLEL_NITER / REMOVE_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_remove_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            remove_i++;
        } /* end if */

        /* Launch remove_all test */
        if(i >= remove_all_i * PARALLEL_NITER / REMOVE_ALL_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_remove_all_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            remove_all_i++;
        } /* end if */

        /* Launch terminate_engine test */
        if(i >= terminate_engine_i * PARALLEL_NITER / TERMINATE_ENGINE_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_terminate_engine_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            terminate_engine_i++;
        } /* end if */

        /* Launch fractal test */
        if(i >= fractal_i * PARALLEL_NITER / FRACTAL_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_fractal_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            fractal_i++;
        } /* end if */

        /* Launch fractal_nodep test */
        if(i >= fractal_nodep_i * PARALLEL_NITER / FRACTAL_NODEP_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_fractal_nodep_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            fractal_nodep_i++;
        } /* end if */

        /* Launch create_remove test */
        if(i >= create_remove_i * PARALLEL_NITER / CREATE_REMOVE_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_create_remove_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            create_remove_i++;
        } /* end if */

        /* Launch create_remove_all test */
        if(i >= create_remove_all_i * PARALLEL_NITER / CREATE_REMOVE_ALL_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_create_remove_all_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            create_remove_all_i++;
        } /* end if */

        /* Launch pileup test */
        if(i >= pileup_i * PARALLEL_NITER / PILEUP_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_pileup_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            pileup_i++;
        } /* end if */

        /* Launch num_threads test */
        if(i >= num_threads_i * PARALLEL_NITER / NUM_THREADS_NITER) {
            if(AXEgenerate_task_id(meta_engine, &tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEcreate_task(meta_engine, tmp_task, 0, NULL, 0, NULL, test_num_threads_helper, &helper_data, NULL) != AXE_SUCCEED)
                TEST_ERROR;
            if(AXEfinish(meta_engine, tmp_task) != AXE_SUCCEED)
                TEST_ERROR;
            num_threads_i++;
        } /* end if */
    } /* end for */

    /* Terminate meta engine and wait for all tasks to complete */
    AXE_test_exclude_close_on(meta_engine);
    if(AXEterminate_engine(meta_engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;
    meta_engine_init = FALSE;

    /* Release threads used by meta engine */
    MAX_NTHREADS_RELEASE(num_threads_meta, TEST_ERROR);

    /* Verify results */
    if(OPA_load_int(&helper_data.nfailed) != 0)
        TEST_ERROR;
    assert(simple_i == (SIMPLE_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(necessary_i == (NECESSARY_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(sufficient_i == (SUFFICIENT_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(barrier_i == (BARRIER_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(get_op_data_i == (GET_OP_DATA_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(finish_all_i == (FINISH_ALL_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(free_op_data_i == (FREE_OP_DATA_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(remove_i == (REMOVE_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(remove_all_i == (REMOVE_ALL_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(terminate_engine_i == (TERMINATE_ENGINE_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(fractal_i == (FRACTAL_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(fractal_nodep_i == (FRACTAL_NODEP_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(create_remove_i == (CREATE_REMOVE_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(create_remove_all_i == (CREATE_REMOVE_ALL_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(pileup_i == (PILEUP_NITER * niter - 1) / PARALLEL_NITER + 1);
    assert(num_threads_i == (NUM_THREADS_NITER * niter - 1) / PARALLEL_NITER + 1);
    if(OPA_load_int(&helper_data.ncomplete) != simple_i + necessary_i + sufficient_i + barrier_i + get_op_data_i + finish_all_i + free_op_data_i + remove_i + remove_all_i + terminate_engine_i + fractal_i + fractal_nodep_i + create_remove_i + create_remove_all_i + pileup_i + num_threads_i)
        TEST_ERROR;

    /* Destroy parallel mutex */
    if(0 != pthread_mutex_destroy(&parallel_mutex))
        TEST_ERROR;

    /* Terminate internal engine */
    AXE_test_exclude_close_on(helper_data.engine);
    if(AXEterminate_engine(helper_data.engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Release threads used by internal engine */
    MAX_NTHREADS_RELEASE(num_threads_int, TEST_ERROR);

    /* Destroy engine attribute */
    if(AXEengine_attr_destroy(&engine_attr) != AXE_SUCCEED)
        TEST_ERROR;

    PASSED();

#ifdef AXE_DEBUG_PERF
    printf(" Adds: %d, Spins in add_common(): %d, Spins in finish(): %d\n", OPA_load_int(&AXE_debug_nadds), OPA_load_int(&AXE_debug_nspins_add), OPA_load_int(&AXE_debug_nspins_finish));
#endif /* AXE_DEBUG_PERF */

    return 0;

error:
    if(meta_engine_init) {
        (void)AXEterminate_engine(meta_engine, FALSE);
        MAX_NTHREADS_RELEASE(num_threads_meta, );
    } /* end if */
    (void)AXEterminate_engine(helper_data.engine, FALSE);
    MAX_NTHREADS_RELEASE(num_threads_int, );
    (void)AXEengine_attr_destroy(&engine_attr);
    (void)pthread_mutex_destroy(&parallel_mutex);

    return 1;
} /* end test_parallel() */


/*-------------------------------------------------------------------------
 * Function:    main
 *
 * Purpose:     Loops over the numbers of threads in the num_threads_g
 *              array, performing each test with each number of threads.
 *
 * Return:      Success: 0
 *              Failure: 1
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
int
main(int argc, char **argv)
{
    int i;
    int nerrors = 0;

    /* Initialize framework for limiting maximum number of threads */
    MAX_NTHREADS_INIT(puts("FAILED to initialize!\n"); exit(1));

    /* Loop over number of threads */
    for(i = 0; i < (sizeof(num_threads_g) / sizeof(num_threads_g[0])); i++) {
        /* Check if we can run with this many threads */
        MAX_NTHREADS_CHECK_STATIC_IF(num_threads_g[i]) {
            printf("----Testing with %d threads----\n", (int)num_threads_g[i]); fflush(stdout);

            /* The tests */
            nerrors += test_serial(test_simple_helper, num_threads_g[i], SIMPLE_NITER / iter_reduction_g[i], TRUE, "simple tasks");
            nerrors += test_serial(test_necessary_helper, num_threads_g[i], NECESSARY_NITER / iter_reduction_g[i], TRUE, "necessary task parents");
            nerrors += test_serial(test_sufficient_helper, num_threads_g[i], SUFFICIENT_NITER / iter_reduction_g[i], TRUE, "sufficient task parents");
            nerrors += test_serial(test_barrier_helper, num_threads_g[i], BARRIER_NITER / iter_reduction_g[i], FALSE, "barrier tasks");
            nerrors += test_serial(test_get_op_data_helper, num_threads_g[i], GET_OP_DATA_NITER / iter_reduction_g[i], TRUE, "AXEget_op_data()");
            nerrors += test_serial(test_finish_all_helper, num_threads_g[i], FINISH_ALL_NITER / iter_reduction_g[i], TRUE, "AXEfinish_all()");
            nerrors += test_serial(test_free_op_data_helper, num_threads_g[i], FREE_OP_DATA_NITER / iter_reduction_g[i], TRUE, "free_op_data callback");
            nerrors += test_serial(test_remove_helper, num_threads_g[i], REMOVE_NITER / iter_reduction_g[i], TRUE, "AXEremove()");
            nerrors += test_serial(test_remove_all_helper, num_threads_g[i], REMOVE_ALL_NITER / iter_reduction_g[i], FALSE, "AXEremove_all()");
            nerrors += test_serial(test_terminate_engine_helper, num_threads_g[i], TERMINATE_ENGINE_NITER / iter_reduction_g[i], FALSE, "AXEterminate_engine()");
            nerrors += test_serial(test_fractal_helper, num_threads_g[i], FRACTAL_NITER / iter_reduction_g[i], TRUE, "fractal task creation");
            nerrors += test_serial(test_fractal_nodep_helper, num_threads_g[i], FRACTAL_NODEP_NITER / iter_reduction_g[i], TRUE, "fractal task creation without dependencies");
            nerrors += test_serial(test_create_remove_helper, num_threads_g[i], CREATE_REMOVE_NITER / iter_reduction_g[i], TRUE, "simultaneously creating and removing tasks");
            nerrors += test_serial(test_create_remove_all_helper, num_threads_g[i], CREATE_REMOVE_ALL_NITER / iter_reduction_g[i], FALSE, "simultaneously creating and removing all tasks");
            nerrors += test_serial(test_pileup_helper, num_threads_g[i], PILEUP_NITER / iter_reduction_g[i], FALSE, "simultaneously creating and removing tasks with many threads");
            MAX_NTHREADS_CHECK_STATIC_IF(PARALLEL_NUM_THREADS_META + num_threads_g[i] + (num_threads_g[i] > 2 ? num_threads_g[i] : 2))
                nerrors += test_parallel(PARALLEL_NUM_THREADS_META, num_threads_g[i], PARALLEL_NITER / iter_reduction_g[i]);
        } /* end MAX_NTHREADS_CHECK_STATIC_IF */
    } /* end for */

    printf("----Tests with fixed number of threads----\n"); fflush(stdout);
    /* Check if we can run with 2 threads */
    MAX_NTHREADS_CHECK_STATIC_IF(2)
        nerrors += test_serial(test_num_threads_helper, 2, NUM_THREADS_NITER, FALSE, "number of threads");
    /* Check if we can run with 1 thread */
    MAX_NTHREADS_CHECK_STATIC_IF(1)
        nerrors += test_serial(test_id_helper, 1, 1, FALSE, "task ids");
    /* Check if we can run with 1 thread */
    MAX_NTHREADS_CHECK_STATIC_IF(1)
        nerrors += test_serial(test_attr_helper, 1, 1, FALSE, "engine attributes");

    /* Free memory allocated for limiting number of threads */
    MAX_NTHREADS_FREE(puts("FAILED to shut down properly!\n"); exit(1));

    /* Print message about failure or success and exit */
    if(nerrors) {
        printf("***** %d TEST%s FAILED! *****\n",
                nerrors, 1 == nerrors ? "" : "S");
        return 1;
    } /* end if */
    else {
        printf("All tests passed.\n");
        return 0;
    } /* end else */
} /* end main () */

