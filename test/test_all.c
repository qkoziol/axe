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

/* Add checking for other params */
typedef struct {
    basic_task_shared_t *shared;
    int failed;
    int run_order;
    pthread_mutex_t *mutex;
} basic_task_t;


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

    /* Lock and unlock the mutex, if provided, to prevent exectuion until we are
     * allowed */
    if(task_data->mutex) {
        if(0 != pthread_mutex_lock(task_data->mutex))
            task_data->failed = 1;
        if(0 != pthread_mutex_unlock(task_data->mutex))
            task_data->failed = 1;
    } /* end if */

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
    basic_task_shared_t shared_task_data;\
    int i;

    TESTING("simple tasks");

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].failed = 0;
        task_data[i].mutex = NULL;
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
    if(AXEwait(task[0]) < 0)
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
    if(AXEwait(task[0]) < 0)
        TEST_ERROR;
    if(AXEwait(task[1]) < 0)
        TEST_ERROR;
    if(AXEwait(task[2]) < 0)
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
    if(task_data[0].run_order == -1)
        TEST_ERROR;
    if(task_data[0].run_order == -1)
        TEST_ERROR;
    if(task_data[0].run_order == -1)
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
    if(AXEwait(task[1]) < 0)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if(task_data[1].run_order != 1)
        TEST_ERROR;
    for(i = 2; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 2)
        TEST_ERROR;

    /* Close tasks */
    if(AXEget_status(task[0], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
    if(AXEget_status(task[1], &status) != AXE_SUCCEED)
        TEST_ERROR;
    if(status != AXE_TASK_DONE)
        TEST_ERROR;
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
    if(AXEwait(task[1]) < 0)
        TEST_ERROR;
    if(AXEwait(task[2]) < 0)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].run_order != 0)
        TEST_ERROR;
    if((task_data[1].run_order < 1) || (task_data[1].run_order > 2))
        TEST_ERROR;
    if((task_data[2].run_order < 1) || (task_data[2].run_order > 2))
        TEST_ERROR;
    for(i = 3; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].run_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 3)
        TEST_ERROR;

    /* Close tasks */
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
    if(AXEwait(task[2]) < 0)
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
        if(AXEwait(task[2]) < 0)
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
        if(AXEwait(task[0]) < 0)
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
        if(AXEwait(task[3]) < 0)
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
    if(AXEwait(task[9]) < 0)
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
    if(task_data[9].run_order != 9)
        TEST_ERROR;
    for(i = 1; i <= 8; i++)
        if(task_data[i].run_order == -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 10)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i < 10; i++)
        if(AXEfinish(task[i]) != AXE_SUCCEED)
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
    if(AXEwait(task[1]) < 0)
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
    if(AXEwait(task[1]) < 0)
        TEST_ERROR;
    if(AXEwait(task[2]) < 0)
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
    if(AXEwait(task[0]) < 0)
        TEST_ERROR;
    if(AXEwait(task[1]) < 0)
        TEST_ERROR;
    if(AXEwait(task[2]) < 0)
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
        if(AXEwait(task[2]) < 0)
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
        if(AXEwait(task[1]) < 0)
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
        if(AXEwait(task[i]) < 0)
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
    for(i = 1; i <= 9; i++)
        if(task_data[i].run_order == -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 10)
        TEST_ERROR;

    /* Close tasks */
    for(i = 0; i < 10; i++)
        if(AXEfinish(task[i]) != AXE_SUCCEED)
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
main(int argc, char **argv)
{
    int i;
    int nerrors = 0;

    /* Loop over number of threads */
    for(i = 0; i < (sizeof(num_threads_g) / sizeof(num_threads_g[0])); i++) {
        printf("----Testing with %d threads----\n", (int)num_threads_g[i]);

        /* The tests */
        nerrors += test_simple(num_threads_g[i]);
        nerrors += test_necessary(num_threads_g[i]);
        nerrors += test_sufficient(num_threads_g[i]);
    } /* end for */

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

