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


#include "axe_test.h"


/*
 * Typedefs
 */
typedef struct {
    int max_ncalls;
    OPA_int_t ncalls;
} basic_task_shared_t;

typedef struct {
    basic_task_shared_t *shared;
    int failed;
    int run_order;
    size_t num_necessary_parents;
    size_t num_sufficient_parents;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
    pthread_mutex_t *cond_mutex;
    int cond_signal_sent;
} basic_task_t;


/*
 * Macros
 */
#define FRACTAL_NCHILDREN 2
#define FRACTAL_NTASKS 10000


/*
 * Variables
 */
size_t num_threads_g[] = {1, 2, 3, 5, 10};


void
basic_task_worker(size_t num_necessary_parents, AXE_task_t necessary_parents[],
    size_t num_sufficient_parents, AXE_task_t sufficient_parents[],
    void *_task_data)
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
        if(AXEfinish(necessary_parents[i]) != AXE_SUCCEED)
            task_data->failed = 1;
    for(i = 0; i < num_sufficient_parents; i++)
        if(AXEfinish(sufficient_parents[i]) != AXE_SUCCEED)
            task_data->failed = 1;

    return;
} /* end basic_task_worker() */


int test_simple(size_t num_threads)
{
    AXE_engine_t engine;
    AXE_task_t task[3];
    AXE_status_t status;
    basic_task_t task_data[3];
    basic_task_shared_t shared_task_data;
    int i;

    TESTING("simple tasks");

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
        task_data[i].cond_signal_sent = 0;
    } /* end for */

    /* Create AXE engine */
    if(AXEcreate_engine(num_threads, &engine) != AXE_SUCCEED)
        TEST_ERROR;


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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for task to complete */
    if(AXEwait(task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[1], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[2], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[2], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[2]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 3: No task id requested
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 1;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create simple task */
    if(AXEcreate_task(engine, NULL, 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 4: No worker task
     */
    /* Create simple task */
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, NULL, NULL, NULL)
            != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for task to complete */
    if(AXEwait(task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;

    /* Close task */
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 5: No task id requested and no worker task
     */
    /* Create simple task */
    if(AXEcreate_task(engine, NULL, 0, NULL, 0, NULL, NULL, NULL, NULL)
            != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Close
     */
    /* Terminate engine */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    PASSED();
    return 0;

error:
    (void)AXEterminate_engine(engine, FALSE);

    return 1;
} /* end test_simple() */


int
test_necessary(size_t num_threads)
{
    AXE_engine_t engine;
    AXE_task_t task[10];
    AXE_task_t parent_task[10];
    AXE_status_t status;
    basic_task_t task_data[10];
    basic_task_shared_t shared_task_data;
    pthread_mutex_t mutex1, mutex2;
    int i;

    TESTING("necessary task parents");

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

    /* Create AXE engine */
    if(AXEcreate_engine(num_threads, &engine) != AXE_SUCCEED)
        TEST_ERROR;


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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second task */
    if(AXEcreate_task(engine, &task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[1]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[1]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create first child task */
    if(AXEcreate_task(engine, &task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second child task */
    if(AXEcreate_task(engine, &task[2], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[2], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[2]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second parent task */
    if(AXEcreate_task(engine, &task[1], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child task */
    parent_task[0] = task[0];
    parent_task[1] = task[1];
    if(AXEcreate_task(engine, &task[2], 2, parent_task, 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[2], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[2]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 4: Three parents, one child, ordering tested with mutexes
     */
    /* Only test with at least 3 worker threads, otherwise it could deadlock
     * because this test assumes parallel execution */
    if(num_threads >= 3) {
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
        if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
                &task_data[0], NULL) != AXE_SUCCEED)
            TEST_ERROR;

        /* Create second parent task */
        if(AXEcreate_task(engine, &task[1], 0, NULL, 0, NULL, basic_task_worker,
                &task_data[1], NULL) != AXE_SUCCEED)
            TEST_ERROR;

        /* Create third parent task */
        if(AXEcreate_task(engine, &task[2], 0, NULL, 0, NULL, basic_task_worker,
                &task_data[2], NULL) != AXE_SUCCEED)
            TEST_ERROR;

        /* Create child task */
        parent_task[0] = task[0];
        parent_task[1] = task[1];
        parent_task[2] = task[2];
        if(AXEcreate_task(engine, &task[3], 3, parent_task, 0, NULL, basic_task_worker,
                &task_data[3], NULL) != AXE_SUCCEED)
            TEST_ERROR;

        /* Wait for third parent task to complete */
        if(AXEwait(task[2]) != AXE_SUCCEED)
            TEST_ERROR;

        /* Make sure the blocked parent tasks have not yet completed, and the
         * child has not been scheduled */
        if(AXEget_status(task[0], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
        if(AXEget_status(task[1], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
        if(AXEget_status(task[2], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(task[3], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;

        /* Release first mutex */
        if(0 != pthread_mutex_unlock(task_data[0].mutex))
            TEST_ERROR;

        /* Wait for first parent task to complete */
        if(AXEwait(task[0]) != AXE_SUCCEED)
            TEST_ERROR;

        /* Make sure the blocked parent task has not run yet, and the child has
         * not been scheduled */
        if(AXEget_status(task[0], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(task[1], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
        if(AXEget_status(task[2], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(task[3], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;

        /* Release second mutex */
        if(0 != pthread_mutex_unlock(task_data[1].mutex))
            TEST_ERROR;

        /* Wait for child task to complete */
        if(AXEwait(task[3]) != AXE_SUCCEED)
            TEST_ERROR;

        /* Verify results */
        if(AXEget_status(task[0], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(task[1], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(task[2], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(task[3], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[0]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(task[1]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(task[2]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(task[3]) != AXE_SUCCEED)
            TEST_ERROR;
        for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
            task_data[i].mutex = NULL;
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create secondary parent tasks */
    for(i = 1; i <= 8; i++)
        if(AXEcreate_task(engine, &task[i], 1, &task[0], 0, NULL, basic_task_worker,
                &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;

    /* Create child task */
    if(AXEcreate_task(engine, &task[9], 9, task, 0, NULL, basic_task_worker,
            &task_data[9], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Make sure the primary parent task has not yet completed, and the other
     * tasks have not been scheduled */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 1; i <= 9; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;
    } /* end for */

    /* Release mutex */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[9]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i < 10; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[i]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child tasks */
    for(i = 1; i <= 9; i++)
        if(AXEcreate_task(engine, &task[i], 1, &task[0], 0, NULL, basic_task_worker,
                &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;

    /* Make sure the primary parent task has not yet completed, and the other
     * tasks have not been scheduled */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 1; i <= 9; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;
    } /* end for */

    /* Release mutex */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete */
    for(i = 1; i <= 9; i++)
        if(AXEwait(task[i]) != AXE_SUCCEED)
            TEST_ERROR;

    /* Verify results */
    for(i = 0; i < 10; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[i]) != AXE_SUCCEED)
            TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].mutex = NULL;


    /*
     * Close
     */
    /* Terminate engine */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Destroy mutexes */
    if(0 != pthread_mutex_destroy(&mutex1))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&mutex2))
        TEST_ERROR;

    PASSED();
    return 0;

error:
    (void)AXEterminate_engine(engine, FALSE);

    (void)pthread_mutex_destroy(&mutex1);
    (void)pthread_mutex_destroy(&mutex2);

    return 1;
} /* end test_necessary() */


int
test_sufficient(size_t num_threads)
{
    AXE_engine_t engine;
    AXE_task_t task[10];
    AXE_task_t parent_task[10];
    AXE_status_t status;
    basic_task_t task_data[10];
    basic_task_shared_t shared_task_data;
    pthread_mutex_t mutex1, mutex2;
    int i;

    TESTING("sufficient task parents");

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

    /* Create AXE engine */
    if(AXEcreate_engine(num_threads, &engine) != AXE_SUCCEED)
        TEST_ERROR;


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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second task */
    if(AXEcreate_task(engine, &task[1], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[1]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[1]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create first child task */
    if(AXEcreate_task(engine, &task[1], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second child task */
    if(AXEcreate_task(engine, &task[2], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[2], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[2]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second parent task */
    if(AXEcreate_task(engine, &task[1], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child task */
    parent_task[0] = task[0];
    parent_task[1] = task[1];
    if(AXEcreate_task(engine, &task[2], 0, NULL, 2, parent_task, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[2], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[2]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 4: Two parents, one child, ordering tested with mutexes
     */
    /* Only test with at least 2 worker threads, otherwise it could deadlock
     * because this test assumes parallel execution */
    if(num_threads >= 2) {
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
        if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
                &task_data[0], NULL) != AXE_SUCCEED)
            TEST_ERROR;

        /* Create second parent task */
        if(AXEcreate_task(engine, &task[1], 0, NULL, 0, NULL, basic_task_worker,
                &task_data[1], NULL) != AXE_SUCCEED)
            TEST_ERROR;

        /* Create child task */
        parent_task[0] = task[0];
        parent_task[1] = task[1];
        if(AXEcreate_task(engine, &task[2], 0, NULL, 2, parent_task, basic_task_worker,
                &task_data[2], NULL) != AXE_SUCCEED)
            TEST_ERROR;

        /* Make sure the parent tasks have not finished, and child has not been
         * scheduled */
        if(AXEget_status(task[0], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
        if(AXEget_status(task[1], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
        if(AXEget_status(task[2], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;

        /* Release first mutex */
        if(0 != pthread_mutex_unlock(task_data[0].mutex))
            TEST_ERROR;

        /* Wait for child task to complete */
        if(AXEwait(task[2]) != AXE_SUCCEED)
            TEST_ERROR;

        /* Make sure the first parent and child have completed, and the second
         * parent has not finished */
        if(AXEget_status(task[0], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(task[1], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
        if(AXEget_status(task[2], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;

        /* Release second mutex */
        if(0 != pthread_mutex_unlock(task_data[1].mutex))
            TEST_ERROR;

        /* Wait for second parent task to complete */
        if(AXEwait(task[1]) != AXE_SUCCEED)
            TEST_ERROR;

        /* Verify results */
        if(AXEget_status(task[0], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(task[1], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_TASK_DONE)
            TEST_ERROR;
        if(AXEget_status(task[2], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[0]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(task[1]) != AXE_SUCCEED)
            TEST_ERROR;
        if(AXEfinish(task[2]) != AXE_SUCCEED)
            TEST_ERROR;
        for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
            task_data[i].mutex = NULL;
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create secondary parent tasks */
    for(i = 1; i <= 8; i++)
        if(AXEcreate_task(engine, &task[i], 0, NULL, 1, &task[0], basic_task_worker,
                &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;

    /* Create child task */
    if(AXEcreate_task(engine, &task[9], 0, NULL, 9, task, basic_task_worker,
            &task_data[9], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Make sure the primary parent task has not yet completed, and the other
     * tasks have not been scheduled */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 1; i <= 9; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;
    } /* end for */

    /* Release mutex */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete */
    for(i = 1; i <= 9; i++)
        if(AXEwait(task[i]) != AXE_SUCCEED)
            TEST_ERROR;

    /* Verify results */
    for(i = 0; i < 10; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[i]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child tasks */
    for(i = 1; i <= 9; i++)
        if(AXEcreate_task(engine, &task[i], 0, NULL, 1, &task[0], basic_task_worker,
                &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;

    /* Make sure the primary parent task has not yet completed, and the other
     * tasks have not been scheduled */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 1; i <= 9; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;
    } /* end for */

    /* Release mutex */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete */
    for(i = 1; i <= 9; i++)
        if(AXEwait(task[i]) != AXE_SUCCEED)
            TEST_ERROR;

    /* Verify results */
    for(i = 0; i < 10; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[i]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second parent task */
    if(AXEcreate_task(engine, &task[1], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child task */
    if(AXEcreate_task(engine, &task[2], 1, &task[0], 1, &task[1],
            basic_task_worker, &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[2], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[2]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Close
     */
    /* Terminate engine */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Destroy mutexes */
    if(0 != pthread_mutex_destroy(&mutex1))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&mutex2))
        TEST_ERROR;

    PASSED();
    return 0;

error:
    (void)AXEterminate_engine(engine, FALSE);

    (void)pthread_mutex_destroy(&mutex1);
    (void)pthread_mutex_destroy(&mutex2);

    return 1;
} /* end test_sufficient() */


int
test_barrier(size_t num_threads)
{
    AXE_engine_t engine;
    AXE_task_t task[11];
    AXE_task_t parent_task[2];
    AXE_status_t status;
    basic_task_t task_data[11];
    basic_task_shared_t shared_task_data;
    pthread_mutex_t mutex1;
    int i;

    TESTING("barrier tasks");

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

    /* Create AXE engine */
    if(AXEcreate_engine(num_threads, &engine) != AXE_SUCCEED)
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
    if(AXEcreate_barrier_task(engine, &task[0], basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create barrier task */
    if(AXEcreate_barrier_task(engine, &task[1], basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[1]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[1]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create barrier task */
    if(AXEcreate_barrier_task(engine, &task[1], basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child task */
    if(AXEcreate_task(engine, &task[2], 1, &task[1], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[2], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[2]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child tasks */
    if(AXEcreate_task(engine, &task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[2], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create barrier task */
    if(AXEcreate_barrier_task(engine, &task[3], basic_task_worker,
            &task_data[3], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[3]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i <= 3; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[i]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child tasks */
    if(AXEcreate_task(engine, &task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[2], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create barrier task */
    if(AXEcreate_barrier_task(engine, &task[3], basic_task_worker,
            &task_data[3], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Make sure the parent task has not finished, and other tasks have not been
     * scheduled */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 0; i <= 3; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
    } /* end for */

    /* Release mutex */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[3]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i <= 3; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[i]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second-level child tasks */
    for(i = 1; i <= 3; i++)
        if(AXEcreate_task(engine, &task[i], 1, &task[0], 0, NULL,
                basic_task_worker, &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;

    /* Create sufficient child task */
    parent_task[0] = task[0];
    parent_task[1] = task[2];
    if(AXEcreate_task(engine, &task[4], 0, NULL, 2, parent_task,
            basic_task_worker, &task_data[4], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create first barrier task */
    if(AXEcreate_barrier_task(engine, &task[5], basic_task_worker,
            &task_data[5], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second barrier task */
    if(AXEcreate_barrier_task(engine, &task[6], basic_task_worker,
            &task_data[6], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[6]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i <= 6; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[i]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second-level child tasks */
    for(i = 1; i <= 3; i++)
        if(AXEcreate_task(engine, &task[i], 1, &task[0], 0, NULL,
                basic_task_worker, &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;

    /* Create sufficient child task */
    parent_task[0] = task[0];
    parent_task[1] = task[2];
    if(AXEcreate_task(engine, &task[4], 0, NULL, 2, parent_task,
            basic_task_worker, &task_data[4], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create first barrier task */
    if(AXEcreate_barrier_task(engine, &task[5], basic_task_worker,
            &task_data[5], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second barrier task */
    if(AXEcreate_barrier_task(engine, &task[6], basic_task_worker,
            &task_data[6], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Make sure the parent task has not finished, and other tasks have not been
     * scheduled */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 0; i <= 6; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
            TEST_ERROR;
    } /* end for */

    /* Release mutex */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[6]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i <= 6; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[i]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create secondary parent tasks */
    for(i = 1; i <= 9; i++)
        if(AXEcreate_task(engine, &task[i], 1, &task[0], 0, NULL, basic_task_worker,
                &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;

    /* Create barrier task */
    if(AXEcreate_barrier_task(engine, &task[10], basic_task_worker,
            &task_data[10], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Make sure the primary parent task has not yet completed, and the other
     * tasks have not been scheduled */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status == AXE_TASK_DONE) || (status == AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 1; i <= 10; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
            TEST_ERROR;
        if(status != AXE_WAITING_FOR_PARENT)
            TEST_ERROR;
    } /* end for */

    /* Release mutex */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[10]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i < 11; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[i]) != AXE_SUCCEED)
            TEST_ERROR;
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].mutex = NULL;


    /*
     * Close
     */
    /* Terminate engine */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Destroy mutex */
    if(0 != pthread_mutex_destroy(&mutex1))
        TEST_ERROR;

    PASSED();
    return 0;

error:
    (void)AXEterminate_engine(engine, FALSE);

    (void)pthread_mutex_destroy(&mutex1);

    return 1;
} /* end test_barrier() */


int
test_get_op_data(size_t num_threads)
{
    AXE_engine_t engine;
    AXE_task_t task;
    AXE_status_t status;
    basic_task_t task_data;
    void *op_data;
    basic_task_shared_t shared_task_data;

    TESTING("AXEget_op_data()");

    /* Initialize task data struct */
    task_data.shared = &shared_task_data;
    task_data.failed = 0;
    task_data.mutex = NULL;
    task_data.cond = NULL;
    task_data.cond_mutex = NULL;
    task_data.cond_signal_sent = 0;

    /* Create AXE engine */
    if(AXEcreate_engine(num_threads, &engine) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 1: Single task
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 1;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    task_data.run_order = -1;

    /* Create barrier task */
    if(AXEcreate_task(engine, &task, 0, NULL, 0, NULL, basic_task_worker,
            &task_data, NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Check that op_data returned is task_data */
    if(AXEget_op_data(task, &op_data) != AXE_SUCCEED)
        TEST_ERROR;
    if(op_data != (void *)&task_data)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task) != AXE_SUCCEED)
        TEST_ERROR;

    /* Check that op_data returned is task_data */
    if(AXEget_op_data(task, &op_data) != AXE_SUCCEED)
        TEST_ERROR;
    if(op_data != (void *)&task_data)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task, &status) != AXE_SUCCEED)
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
    if(AXEfinish(task) != AXE_SUCCEED)
        TEST_ERROR;

    /*
     * Close
     */
    /* Terminate engine */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    PASSED();
    return 0;

error:
    (void)AXEterminate_engine(engine, FALSE);

    return 1;
} /* end test_get_op_data() */


int
test_finish_all(size_t num_threads)
{
    AXE_engine_t engine;
    AXE_task_t task[2];
    AXE_status_t status;
    basic_task_t task_data[2];
    basic_task_shared_t shared_task_data;
    int i;

    TESTING("AXEget_finish_all()");

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
        task_data[i].cond_signal_sent = 0;
    } /* end for */

    /* Create AXE engine */
    if(AXEcreate_engine(num_threads, &engine) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[1], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(task[1]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
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
    if(AXEfinish_all(2, task) != AXE_SUCCEED)
        TEST_ERROR;

    /* If we ever move to a more robust identifier system or add a way to
     * manipulate and retrieve a task's reference count, we should make sure
     * AXEfinish_all() actually closed the tasks here */


    /*
     * Close
     */
    /* Terminate engine */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    PASSED();
    return 0;

error:
    (void)AXEterminate_engine(engine, FALSE);

    return 1;
} /* end test_finish_all() */


typedef struct free_op_data_t {
    OPA_int_t ncalls;
    pthread_cond_t *cond;
    pthread_mutex_t *cond_mutex;
    int failed;
} free_op_data_t;


/* "free_op_data" callback for free_op_data test.  Does not actually free the
 * op data, just marks that it has been called for the specified op_data and
 * sends a signal. */
void
free_op_data_worker(void *_task_data)
{
    free_op_data_t *task_data = (free_op_data_t *)_task_data;

    assert(task_data);
    assert(task_data->cond);
    assert(task_data->cond_mutex);

    /* Lock the condition mutex */
    if(0 != pthread_mutex_lock(task_data->cond_mutex))
        task_data->failed = 1;
    OPA_incr_int(&task_data->ncalls);
    if(0 != pthread_cond_signal(task_data->cond))
        task_data->failed = 1;
    if(0 != pthread_mutex_unlock(task_data->cond_mutex))
        task_data->failed = 1;

    return;
} /* end free_op_data_worker() */


int test_free_op_data(size_t num_threads)
{
    AXE_engine_t engine;
    AXE_task_t task[3];
    free_op_data_t task_data[3];
    pthread_cond_t cond;
    pthread_mutex_t cond_mutex;
    int i;

    TESTING("free_op_data callback");


    /* Initialize mutex and condition variable */
    if(0 != pthread_cond_init(&cond, NULL))
        TEST_ERROR;
    if(0 != pthread_mutex_init(&cond_mutex, NULL))
        TEST_ERROR;

    /* Create AXE engine */
    if(AXEcreate_engine(num_threads, &engine) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Test 1: Single task
     */
    /* Initialize task_data */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        OPA_store_int(&task_data[i].ncalls, 0);
        task_data[i].failed = 0;
    } /* end for */
    task_data[0].cond = &cond;
    task_data[0].cond_mutex = &cond_mutex;

    /* Create simple task */
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, NULL, &task_data[0],
            free_op_data_worker) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for task to complete */
    if(AXEwait(task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify free_op_data has been called the correct number of times */
    if(OPA_load_int(&(task_data[0]).ncalls) != 0)
        TEST_ERROR;
    if(OPA_load_int(&(task_data[1]).ncalls) != 0)
        TEST_ERROR;
    if(OPA_load_int(&(task_data[2]).ncalls) != 0)
        TEST_ERROR;

    /* Close task */
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for condition signal so we know the free_op_data callback has been
     * called */
    if(0 != pthread_mutex_lock(&cond_mutex))
        TEST_ERROR;
    if(OPA_load_int(&(task_data[0]).ncalls) == 0)
        if(0 != pthread_cond_wait(&cond, &cond_mutex))
            TEST_ERROR;
    if(0 != pthread_mutex_unlock(&cond_mutex))
        TEST_ERROR;

    /* Verify free_op_data has been called the correct number of times */
    if(OPA_load_int(&(task_data[0]).ncalls) != 1)
        TEST_ERROR;
    if(OPA_load_int(&(task_data[1]).ncalls) != 0)
        TEST_ERROR;
    if(OPA_load_int(&(task_data[2]).ncalls) != 0)
        TEST_ERROR;


    /*
     * Test 2: Three tasks
     */
    /* Initialize task_data */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        OPA_store_int(&task_data[i].ncalls, 0);
        task_data[i].failed = 0;
        task_data[i].cond = &cond;
        task_data[i].cond_mutex = &cond_mutex;
    } /* end for */

    /* Create tasks */
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, NULL, &task_data[0],
            free_op_data_worker) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[1], 0, NULL, 0, NULL, NULL, &task_data[1],
            free_op_data_worker) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[2], 0, NULL, 0, NULL, NULL, &task_data[2],
            free_op_data_worker) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify free_op_data has been called the correct number of times */
    if(OPA_load_int(&(task_data[0]).ncalls) != 0)
        TEST_ERROR;
    if(OPA_load_int(&(task_data[1]).ncalls) != 0)
        TEST_ERROR;
    if(OPA_load_int(&(task_data[2]).ncalls) != 0)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for condition signal so we know the free_op_data callback has been
     * called for each task */
    if(0 != pthread_mutex_lock(&cond_mutex))
        TEST_ERROR;
    while((OPA_load_int(&(task_data[0]).ncalls) != 1)
            && (OPA_load_int(&(task_data[1]).ncalls) != 1)
            && (OPA_load_int(&(task_data[2]).ncalls) != 1))
        if(0 != pthread_cond_wait(&cond, &cond_mutex))
            TEST_ERROR;
    if(0 != pthread_mutex_unlock(&cond_mutex))
        TEST_ERROR;

    /* Verify free_op_data has been called the correct number of times (arguably
     * redundant, but may catch a strange bug that sees a thread calling
     * free_op_data more than once for a task) */
    if(OPA_load_int(&(task_data[0]).ncalls) != 1)
        TEST_ERROR;
    if(OPA_load_int(&(task_data[0]).ncalls) != 1)
        TEST_ERROR;
    if(OPA_load_int(&(task_data[0]).ncalls) != 1)
        TEST_ERROR;


    /*
     * Close
     */
    /* Terminate engine */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Destroy mutex and condition variable */
    if(0 != pthread_cond_destroy(&cond))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&cond_mutex))
        TEST_ERROR;

    PASSED();
    return 0;

error:
    (void)AXEterminate_engine(engine, FALSE);

    (void)pthread_cond_destroy(&cond);
    (void)pthread_mutex_destroy(&cond_mutex);

    return 1;
} /* end test_free_op_data() */


int
test_remove(size_t num_threads)
{
    AXE_engine_t engine;
    AXE_task_t task[2];
    AXE_status_t status;
    AXE_remove_status_t remove_status;
    basic_task_t task_data[2];
    basic_task_shared_t shared_task_data;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_mutex_t cond_mutex;
    int i;

    TESTING("AXEremove()");

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

    /* Create AXE engine */
    if(AXEcreate_engine(num_threads, &engine) != AXE_SUCCEED)
        TEST_ERROR;


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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
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
    if(AXEremove(task[0], &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_NOT_CANCELED)
        TEST_ERROR;

    /* Verify task status is AXE_TASK_RUNNING */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_RUNNING)
        TEST_ERROR;

    /* Unlock the main mutex to allow the task to proceed */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for task to complete */
    if(AXEwait(task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Try to remove the task.  Should return AXE_ALL_DONE. */
    if(AXEremove(task[0], &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_ALL_DONE)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second task */
    if(AXEcreate_task(engine, &task[1], 1, &task[0], 0, NULL, basic_task_worker,
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
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_RUNNING)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_WAITING_FOR_PARENT)
        TEST_ERROR;

    /* Try to remove the first task.  Should fail. */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEremove(task[0], &remove_status) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Remove the second task.  Should return AXE_CANCELED. */
    if(AXEremove(task[1], &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_CANCELED)
        TEST_ERROR;

    /* Unlock the main mutex to allow the first task to proceed */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete.  Include wait on canceled task to make sure
     * wait correctly returns failure for canceled task. */
    if(AXEwait(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(task[1]) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Try againto remove the first task.  Should still fail. */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEremove(task[0], &remove_status) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[1]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create second task */
    if(AXEcreate_task(engine, &task[1], 0, NULL, 1, &task[0], basic_task_worker,
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
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_RUNNING)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_WAITING_FOR_PARENT)
        TEST_ERROR;

    /* Try to remove the first task.  Should fail. */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEremove(task[0], &remove_status) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Remove the second task.  Should return AXE_CANCELED. */
    if(AXEremove(task[1], &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_CANCELED)
        TEST_ERROR;

    /* Unlock the main mutex to allow the first task to proceed */
    if(0 != pthread_mutex_unlock(task_data[0].mutex))
        TEST_ERROR;

    /* Wait for tasks to complete.  Include wait on canceled task to make sure
     * wait correctly returns failure for canceled task. */
    if(AXEwait(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(task[1]) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Try again to remove the first task.  Should still fail. */
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEremove(task[0], &remove_status) != AXE_FAIL)
        TEST_ERROR;
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
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
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[1]) != AXE_SUCCEED)
        TEST_ERROR;


    /*
     * Close
     */
    /* Terminate engine */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Destroy mutexes and condition variables */
    if(0 != pthread_mutex_destroy(&mutex))
        TEST_ERROR;
    if(0 != pthread_cond_destroy(&cond))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&cond_mutex))
        TEST_ERROR;

    PASSED();
    return 0;

error:
    (void)AXEterminate_engine(engine, FALSE);

    (void)pthread_mutex_destroy(&mutex);
    (void)pthread_cond_destroy(&cond);
    (void)pthread_mutex_destroy(&cond_mutex);

    return 1;
} /* end test_remove() */


int
test_remove_all(size_t num_threads)
{
    AXE_engine_t engine;
    AXE_task_t task[4];
    AXE_status_t status;
    AXE_remove_status_t remove_status;
    basic_task_t task_data[4];
    basic_task_shared_t shared_task_data;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_mutex_t cond_mutex;
    int i;

    TESTING("AXEremove_all()");

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

    /* Create AXE engine */
    if(AXEcreate_engine(num_threads, &engine) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create children */
    if(AXEcreate_task(engine, &task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[2], 1, &task[1], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[3], 0, NULL, 1, &task[0], basic_task_worker,
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
    (void)AXEwait(task[0]);
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify children are canceled and parent is either canceled or done */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status != AXE_TASK_DONE) && (status != AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 1; i <= 3; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
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
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status != AXE_TASK_DONE) && (status != AXE_TASK_CANCELED))
        TEST_ERROR;
    for(i = 1; i <= 3; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[i]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create children */
    if(AXEcreate_task(engine, &task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[2], 1, &task[1], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[3], 0, NULL, 1, &task[0], basic_task_worker,
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
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_RUNNING)
        TEST_ERROR;
    if(AXEget_status(task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_CANCELED)
        TEST_ERROR;
    if(AXEget_status(task[3], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status != AXE_TASK_DONE) && (status != AXE_TASK_CANCELED)
            && (status != AXE_TASK_RUNNING))
        TEST_ERROR;

    /* Unlock the main mutex to allow the first child task to proceed */
    if(0 != pthread_mutex_unlock(task_data[1].mutex))
        TEST_ERROR;

    /* Wait for the child tasks to complete (sufficient child may fail if
     * it was canceled) */
    if(AXEwait(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEbegin_try() != AXE_SUCCEED)
        TEST_ERROR;
    (void)AXEwait(task[3]);
    if(AXEend_try() != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify statuses */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_CANCELED)
        TEST_ERROR;
    if(AXEget_status(task[3], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if((status != AXE_TASK_DONE) && (status != AXE_TASK_CANCELED))
        TEST_ERROR;

    /* Try to remove all the tasks.  Should return AXE_ALL_DONE. */
    if(AXEremove_all(engine, &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_ALL_DONE)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_CANCELED)
        TEST_ERROR;
    if(AXEget_status(task[3], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[i]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create children */
    if(AXEcreate_task(engine, &task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[2], 1, &task[1], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[3], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[3], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for the child tasks to complete  */
    if(AXEwait(task[2]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEwait(task[3]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify statuses */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[3], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;

    /* Try to remove all the tasks.  Should return AXE_ALL_DONE. */
    if(AXEremove_all(engine, &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_ALL_DONE)
        TEST_ERROR;

    /* Verify results */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[2], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[3], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[i]) != AXE_SUCCEED)
            TEST_ERROR;


    /*
     * Close
     */
    /* Terminate engine */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Destroy mutexes and condition variables */
    if(0 != pthread_mutex_destroy(&mutex))
        TEST_ERROR;
    if(0 != pthread_cond_destroy(&cond))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&cond_mutex))
        TEST_ERROR;

    PASSED();
    return 0;

error:
    (void)AXEterminate_engine(engine, FALSE);

    (void)pthread_mutex_destroy(&mutex);
    (void)pthread_cond_destroy(&cond);
    (void)pthread_mutex_destroy(&cond_mutex);

    return 1;
} /* end test_remove_all() */


int
test_terminate_engine(size_t num_threads)
{
    AXE_engine_t engine;
    _Bool engine_init = FALSE;
    AXE_task_t task[4];
    basic_task_t task_data[4];
    basic_task_shared_t shared_task_data;
    int i;

    TESTING("AXEterminate_engine()");

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].mutex = NULL;
        task_data[i].cond = NULL;
        task_data[i].cond_mutex = NULL;
    } /* end for */


    /*
     * Both tests have a configuration of one parent with one two-task chain of
     * necessary children and a single sufficient child
     */
    /*
     * Test 1: Wait all
     */
    /* Create AXE engine */
    if(AXEcreate_engine(num_threads, &engine) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = TRUE;

    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 4;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create parent task */
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create children */
    if(AXEcreate_task(engine, &task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[2], 1, &task[1], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[3], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[3], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Terminate engine, with wait_all set to TRUE */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = FALSE;

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
        {printf("%d\n", (int)task_data[3].run_order); TEST_ERROR;}
    if(task_data[3].num_necessary_parents != 0)
        TEST_ERROR;
    if(task_data[3].num_sufficient_parents != 1)
        TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 4)
        TEST_ERROR;


    /*
     * Test 2: No wait all
     */
    /* Create AXE engine */
    if(AXEcreate_engine(num_threads, &engine) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = TRUE;

    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 4;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].run_order = -1;

    /* Create parent task */
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create children */
    if(AXEcreate_task(engine, &task[1], 1, &task[0], 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[2], 1, &task[1], 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[3], 0, NULL, 1, &task[0], basic_task_worker,
            &task_data[3], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Terminate engine, with wait_all set to FALSE */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;
    engine_init = FALSE;

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
     * Close
     */
    PASSED();
    return 0;

error:
    if(engine_init)
        (void)AXEterminate_engine(engine, FALSE);

    return 1;
} /* end test_terminate_engine() */


int
test_num_threads(void)
{
    AXE_engine_t engine;
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

    TESTING("number of threads");

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

    /* Create AXE engine with 2 threads */
    if(AXEcreate_engine(2, &engine) != AXE_SUCCEED)
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
    for(i = 0; i <= 3; i++)
        if(AXEcreate_task(engine, &task[i], 0, NULL, 0, NULL, basic_task_worker,
                &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;

    /* Repeatedly scan task_data array and wait for signal until 2 tasks are
     * executing.  Do so while holding condition mutex so cond_signal_sent field
     * is useful. */
    if(0 != pthread_mutex_lock(&cond_mutex))
        TEST_ERROR;

    do {
        nrunning = 0;
        for(i = 0; i <= 3; i++) {
            /* Make sure if the signal was sent the task is running */
            AXEget_status(task[i], &status);
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
    for(i = 0; i <= 3; i++) {
        /* Make sure if the signal was sent the task is running */
        AXEget_status(task[i], &status);
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

    /* Remove the scheduled task */
    if(AXEremove(task[sched_i], &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_CANCELED)
        TEST_ERROR;

    /* Unlock main mutex */
    if(0 != pthread_mutex_unlock(&mutex1))
        TEST_ERROR;

    /* Wait for tasks to complete */
    for(i = 0; i <= 3; i++)
        if(i != sched_i)
            if(AXEwait(task[i]) != AXE_SUCCEED)
                TEST_ERROR;

    /* Verify results */
    for(i = 0; i <= 3; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[i]) != AXE_SUCCEED)
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
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AXE_SUCCEED)
        TEST_ERROR;

    /* Create child tasks */
    for(i = 1; i <= 4; i++)
        if(AXEcreate_task(engine, &task[i], 1, &task[0], 0, NULL,
                basic_task_worker, &task_data[i], NULL) != AXE_SUCCEED)
            TEST_ERROR;

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
            AXEget_status(task[i], &status);
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
    for(i = 1; i <= 4; i++) {
        /* Make sure if the signal was sent the task is running */
        AXEget_status(task[i], &status);
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

    /* Remove the scheduled task */
    if(AXEremove(task[sched_i], &remove_status) != AXE_SUCCEED)
        TEST_ERROR;
    if(remove_status != AXE_CANCELED)
        TEST_ERROR;

    /* Unlock child mutex */
    if(0 != pthread_mutex_unlock(&mutex2))
        TEST_ERROR;

    /* Wait for tasks to complete */
    for(i = 1; i <= 4; i++)
        if(i != sched_i)
            if(AXEwait(task[i]) != AXE_SUCCEED)
                TEST_ERROR;

    /* Verify results */
    for(i = 0; i <= 4; i++) {
        if(AXEget_status(task[i], &status) != AXE_SUCCEED)
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
        if(AXEfinish(task[i]) != AXE_SUCCEED)
            TEST_ERROR;


    /*
     * Close
     */
    /* Terminate engine */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
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

    PASSED();
    return 0;

error:
    (void)AXEterminate_engine(engine, FALSE);

    (void)pthread_mutex_destroy(&mutex1);
    (void)pthread_mutex_destroy(&mutex2);
    (void)pthread_cond_destroy(&cond);
    (void)pthread_mutex_destroy(&cond_mutex);

    return 1;
} /* end test_num_threads() */


typedef struct fractal_task_shared_t {
    AXE_engine_t engine;
    OPA_int_t num_tasks_left_start;
    OPA_int_t num_tasks_left_end;
    pthread_cond_t cond;
    pthread_mutex_t cond_mutex;
} fractal_task_shared_t;

typedef struct fractal_task_t {
    fractal_task_shared_t *shared;
    AXE_task_t this_task;
    struct fractal_task_t *child[FRACTAL_NCHILDREN];
    int failed;
} fractal_task_t;


void
fractal_task_worker(size_t num_necessary_parents,
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
        if(AXEfinish(necessary_parents[0]) != AXE_SUCCEED)
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
            /* Create left child */
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
            if(AXEcreate_task(task_data->shared->engine,
                    &task_data->child[i]->this_task, 1, &task_data->this_task,
                    0, NULL, fractal_task_worker, task_data->child[i], NULL)
                    != AXE_SUCCEED)
                task_data->failed = 1;
        } /* end else */
    } /* end for */

    /* Close task */
    if(AXEfinish(task_data->this_task) != AXE_SUCCEED)
        task_data->failed = 1;

    /* Decrement and test the number of tasks left to finish.  If this was the
     * last task to finish, send the signal to wake up the launcher.  Safe to
     * lock the mutex after the decr-and-test for reasons explained in
     * AXE_schedule_wait_all() and AXE_schedule_finish(). */
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


int
test_fractal(size_t num_threads)
{
    AXE_engine_t engine;
    fractal_task_t *parent_task_data;
    fractal_task_shared_t shared_task_data;
    int num_tasks;
    int i;

    TESTING("fractal task creation");

    /* Create AXE engine */
    if(AXEcreate_engine(num_threads, &engine) != AXE_SUCCEED)
        TEST_ERROR;

    /* Initialize shared task data struct */
    shared_task_data.engine = engine;
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
    if(AXEcreate_task(engine, &parent_task_data->this_task, 0, NULL, 0, NULL,
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
    /* Terminate engine */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Destroy mutex and condition variable */
    if(0 != pthread_cond_destroy(&shared_task_data.cond))
        TEST_ERROR;
    if(0 != pthread_mutex_destroy(&shared_task_data.cond_mutex))
        TEST_ERROR;

    PASSED();
    return 0;

error:
    (void)AXEterminate_engine(engine, FALSE);

    (void)pthread_cond_destroy(&shared_task_data.cond);
    (void)pthread_mutex_destroy(&shared_task_data.cond_mutex);

    return 1;
} /* end test_fractal() */


int
main(int argc, char **argv)
{
    int i;
    int nerrors = 0;

    /* Loop over number of threads */
    for(i = 0; i < (sizeof(num_threads_g) / sizeof(num_threads_g[0])); i++) {
        printf("----Testing with %d threads----\n", (int)num_threads_g[i]); fflush(stdout);

        /* The tests */
        nerrors += test_simple(num_threads_g[i]);
        nerrors += test_necessary(num_threads_g[i]);
        nerrors += test_sufficient(num_threads_g[i]);
        nerrors += test_barrier(num_threads_g[i]);
        nerrors += test_get_op_data(num_threads_g[i]);
        nerrors += test_finish_all(num_threads_g[i]);
        nerrors += test_free_op_data(num_threads_g[i]);
        nerrors += test_remove(num_threads_g[i]);
        nerrors += test_remove_all(num_threads_g[i]);
        nerrors += test_terminate_engine(num_threads_g[i]);
        nerrors += test_fractal(num_threads_g[i]);
    } /* end for */
    printf("----Tests with fixed number of threads----\n"); fflush(stdout);
    nerrors += test_num_threads();

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

