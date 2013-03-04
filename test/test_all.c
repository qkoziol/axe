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
    if(AXEwait(task[0]) < 0)
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


    /*
     * Test 3: Sufficient and necessary parents of same task
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
    if(AXEwait(task[3]) < 0)
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
    if(AXEwait(task[3]) < 0)
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
    if(AXEwait(task[6]) < 0)
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
    if(AXEwait(task[6]) < 0)
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
    if(AXEwait(task[10]) < 0)
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
    if(AXEwait(task) < 0)
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
    if(AXEwait(task[0]) < 0)
        TEST_ERROR;
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


/* "free_op_data" callback for free_op_data test.  Does not actually free the
 * op data, just marks that it has been called for the specified op_data */
void
free_op_data_worker(void *_ncalls)
{
    OPA_int_t *ncalls = (OPA_int_t *)_ncalls;

    OPA_incr_int(ncalls);

    return;
} /* end free_op_data_worker() */


int test_free_op_data(size_t num_threads)
{
    AXE_engine_t engine;
    AXE_task_t task[3];
    OPA_int_t ncalls[3];
    int i;

    TESTING("free_op_data callback");


    /*
     * Test 1: Single task
     */
    /* Create AXE engine */
    if(AXEcreate_engine(num_threads, &engine) != AXE_SUCCEED)
        TEST_ERROR;

    /* Initialize ncalls */
    for(i = 0; i < (sizeof(ncalls) / sizeof(ncalls[0])); i++)
        OPA_store_int(&ncalls[i], 0);

    /* Create simple task */
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, NULL, &ncalls[0],
            free_op_data_worker) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for task to complete */
    if(AXEwait(task[0]) < 0)
        TEST_ERROR;

    /* Verify free_op_data has been called the correct number of times */
    if(OPA_load_int(&ncalls[0]) != 0)
        TEST_ERROR;
    if(OPA_load_int(&ncalls[1]) != 0)
        TEST_ERROR;
    if(OPA_load_int(&ncalls[2]) != 0)
        TEST_ERROR;

    /* Close task */
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Terminate engine - necessary because it is possible that, while the user
     * task operation is complete, the task may still be in the process of being
     * shut down internally and thus, until the engine is shut down, there is
     * no guarantee that the free_op_data callback has been invoked */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify free_op_data has been called the correct number of times */
    if(OPA_load_int(&ncalls[0]) != 1)
        TEST_ERROR;
    if(OPA_load_int(&ncalls[1]) != 0)
        TEST_ERROR;
    if(OPA_load_int(&ncalls[2]) != 0)
        TEST_ERROR;

    /*
     * Test 2: Three tasks
     */
    /* Create AXE engine */
    if(AXEcreate_engine(num_threads, &engine) != AXE_SUCCEED)
        TEST_ERROR;

    /* Initialize ncalls */
    for(i = 0; i < (sizeof(ncalls) / sizeof(ncalls[0])); i++)
        OPA_store_int(&ncalls[i], 0);

    /* Create tasks */
    if(AXEcreate_task(engine, &task[0], 0, NULL, 0, NULL, NULL, &ncalls[0],
            free_op_data_worker) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[1], 0, NULL, 0, NULL, NULL, &ncalls[1],
            free_op_data_worker) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEcreate_task(engine, &task[2], 0, NULL, 0, NULL, NULL, &ncalls[2],
            free_op_data_worker) != AXE_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AXEwait(task[0]) < 0)
        TEST_ERROR;
    if(AXEwait(task[1]) < 0)
        TEST_ERROR;
    if(AXEwait(task[2]) < 0)
        TEST_ERROR;

    /* Verify free_op_data has been called the correct number of times */
    if(OPA_load_int(&ncalls[0]) != 0)
        TEST_ERROR;
    if(OPA_load_int(&ncalls[1]) != 0)
        TEST_ERROR;
    if(OPA_load_int(&ncalls[2]) != 0)
        TEST_ERROR;

    /* Close tasks */
    if(AXEfinish(task[0]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[1]) != AXE_SUCCEED)
        TEST_ERROR;
    if(AXEfinish(task[2]) != AXE_SUCCEED)
        TEST_ERROR;

    /* Terminate engine - necessary because it is possible that, while the user
     * task operation is complete, the task may still be in the process of being
     * shut down internally and thus, until the engine is shut down, there is
     * no guarantee that the free_op_data callback has been invoked */
    if(AXEterminate_engine(engine, TRUE) != AXE_SUCCEED)
        TEST_ERROR;

    /* Verify free_op_data has been called the correct number of times */
    if(OPA_load_int(&ncalls[0]) != 1)
        TEST_ERROR;
    if(OPA_load_int(&ncalls[1]) != 1)
        TEST_ERROR;
    if(OPA_load_int(&ncalls[2]) != 1)
        TEST_ERROR;


    /*
     * Close
     */
    PASSED();
    return 0;

error:
    (void)AXEterminate_engine(engine, FALSE);

    return 1;
} /* end test_free_op_data() */


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
        nerrors += test_barrier(num_threads_g[i]);
        nerrors += test_get_op_data(num_threads_g[i]);
        nerrors += test_finish_all(num_threads_g[i]);
        nerrors += test_free_op_data(num_threads_g[i]);
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

