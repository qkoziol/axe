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


#include "ae2_test.h"


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
    int taskno;
    int failed;
    int call_order;
} basic_task_t;


/*
 * Variables
 */
size_t num_threads_g[] = {1, 2, 3, 5, 10};


void
basic_task_worker(size_t num_necessary_parents, AE2_task_t necessary_parents[],
    size_t num_sufficient_parents, AE2_task_t sufficient_parents[],
    void *_task_data)
{
    basic_task_t *task_data = (basic_task_t *)_task_data;
    size_t i;

    /* Make sure this task hasn't been called yet */
    if(task_data->call_order >= 0)
        task_data->failed = 1;

    /* Retrieve and increment number of calls to shared task struct, this is the
     * call order for this task */
    task_data->call_order = OPA_fetch_and_incr_int(&task_data->shared->ncalls);

    /* Make sure we are not going past the expected number of calls */
    if(task_data->call_order >= task_data->shared->max_ncalls)
        task_data->failed = 1;

    /* Decrement ref counts on parent arrays, as required */
    for(i = 0; i < num_necessary_parents; i++)
        if(AE2finish(necessary_parents[i]) != AE2_SUCCEED)
            task_data->failed = 1;
    for(i = 0; i < num_sufficient_parents; i++)
        if(AE2finish(sufficient_parents[i]) != AE2_SUCCEED)
            task_data->failed = 1;

    return;
} /* end basic_task_worker() */


int
test_simple(size_t num_threads)
{
    AE2_engine_t engine;
    AE2_task_t task1, task2, task3;
    AE2_task_t parent_task[10];
    basic_task_t task_data[10];
    basic_task_shared_t shared_task_data;
    int i;

    TESTING("simple task queue");

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].taskno = i;
        task_data[i].failed = 0;
    } /* end for */

    /* Create AE2 engine */
    if(AE2create_engine(num_threads, &engine) != AE2_SUCCEED)
        TEST_ERROR;


    /*
     * Test 1: Single task
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 1;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].call_order = -1;

    /* Create simple task */
    if(AE2create_task(engine, &task1, 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Wait for task to complete */
    if(AE2wait(task1) < 0)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].call_order != 0)
        TEST_ERROR;
    for(i = 1; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].call_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 1)
        TEST_ERROR;

    /* Close task */
    if(AE2finish(task1) != AE2_SUCCEED)
        TEST_ERROR;


    /*
     * Test 2: Two task chain
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 2;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].call_order = -1;

    /* Create first task */
    if(AE2create_task(engine, &task1, 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Create second task */
    if(AE2create_task(engine, &task2, 1, &task1, 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AE2wait(task2) < 0)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].call_order != 0)
        TEST_ERROR;
    if(task_data[1].call_order != 1)
        TEST_ERROR;
    for(i = 2; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].call_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 2)
        TEST_ERROR;

    /* Close tasks */
    if(AE2finish(task1) != AE2_SUCCEED)
        TEST_ERROR;
    if(AE2finish(task2) != AE2_SUCCEED)
        TEST_ERROR;


    /*
     * Test 3: One parent, two children
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 3;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].call_order = -1;

    /* Create parent task */
    if(AE2create_task(engine, &task1, 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Create first child task */
    if(AE2create_task(engine, &task2, 1, &task1, 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Create second child task */
    if(AE2create_task(engine, &task3, 1, &task1, 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AE2wait(task2) < 0)
        TEST_ERROR;
    if(AE2wait(task3) < 0)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].call_order != 0)
        TEST_ERROR;
    if((task_data[1].call_order < 1) || (task_data[1].call_order > 2))
        TEST_ERROR;
    if((task_data[2].call_order < 1) || (task_data[2].call_order > 2))
        TEST_ERROR;
    for(i = 3; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].call_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 3)
        TEST_ERROR;

    /* Close tasks */
    if(AE2finish(task1) != AE2_SUCCEED)
        TEST_ERROR;
    if(AE2finish(task2) != AE2_SUCCEED)
        TEST_ERROR;
    if(AE2finish(task3) != AE2_SUCCEED)
        TEST_ERROR;


    /*
     * Test 4: Two parents, one child
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 3;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].call_order = -1;

    /* Create first parent task */
    if(AE2create_task(engine, &task1, 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Create second parent task */
    if(AE2create_task(engine, &task2, 0, NULL, 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Create child task */
    parent_task[0] = task1;
    parent_task[1] = task2;
    if(AE2create_task(engine, &task3, 2, parent_task, 0, NULL, basic_task_worker,
            &task_data[2], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AE2wait(task3) < 0)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if((task_data[0].call_order < 0) || (task_data[0].call_order > 1))
        TEST_ERROR;
    if((task_data[1].call_order < 0) || (task_data[1].call_order > 1))
        TEST_ERROR;
    if(task_data[2].call_order != 2)
        TEST_ERROR;
    for(i = 3; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].call_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 3)
        TEST_ERROR;

    /* Close tasks */
    if(AE2finish(task1) != AE2_SUCCEED)
        TEST_ERROR;
    if(AE2finish(task2) != AE2_SUCCEED)
        TEST_ERROR;
    if(AE2finish(task3) != AE2_SUCCEED)
        TEST_ERROR;


    /*
     * Close
     */
    /* Terminate engine */
    if(AE2terminate_engine(engine, TRUE) != AE2_SUCCEED)
        TEST_ERROR;

    PASSED();
    return 0;

error:
    (void)AE2terminate_engine(engine, FALSE);

    return 1;
} /* end test_simple() */


int
test_sufficient(size_t num_threads)
{
    AE2_engine_t engine;
    AE2_task_t task1, task2, task3;
    AE2_task_t parent_task[10];
    basic_task_t task_data[10];
    basic_task_shared_t shared_task_data;
    int i;

    TESTING("sufficient parents");

    /* Initialize task data structs */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++) {
        task_data[i].shared = &shared_task_data;
        task_data[i].taskno = i;
        task_data[i].failed = 0;
    } /* end for */

    /* Create AE2 engine */
    if(AE2create_engine(num_threads, &engine) != AE2_SUCCEED)
        TEST_ERROR;


    /*
     * Test 1: Two task chain
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 2;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].call_order = -1;

    /* Create first task */
    if(AE2create_task(engine, &task1, 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Create second task */
    if(AE2create_task(engine, &task2, 0, NULL, 1, &task1, basic_task_worker,
            &task_data[1], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AE2wait(task2) < 0)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].call_order != 0)
        TEST_ERROR;
    if(task_data[1].call_order != 1)
        TEST_ERROR;
    for(i = 2; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].call_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 2)
        TEST_ERROR;

    /* Close tasks */
    if(AE2finish(task1) != AE2_SUCCEED)
        TEST_ERROR;
    if(AE2finish(task2) != AE2_SUCCEED)
        TEST_ERROR;


    /*
     * Test 3: One parent, two children
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 3;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].call_order = -1;

    /* Create parent task */
    if(AE2create_task(engine, &task1, 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Create first child task */
    if(AE2create_task(engine, &task2, 0, NULL, 1, &task1, basic_task_worker,
            &task_data[1], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Create second child task */
    if(AE2create_task(engine, &task3, 0, NULL, 1, &task1, basic_task_worker,
            &task_data[2], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AE2wait(task2) < 0)
        TEST_ERROR;
    if(AE2wait(task3) < 0)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[0].call_order != 0)
        TEST_ERROR;
    if((task_data[1].call_order < 1) || (task_data[1].call_order > 2))
        TEST_ERROR;
    if((task_data[2].call_order < 1) || (task_data[2].call_order > 2))
        TEST_ERROR;
    for(i = 3; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].call_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 3)
        TEST_ERROR;

    /* Close tasks */
    if(AE2finish(task1) != AE2_SUCCEED)
        TEST_ERROR;
    if(AE2finish(task2) != AE2_SUCCEED)
        TEST_ERROR;
    if(AE2finish(task3) != AE2_SUCCEED)
        TEST_ERROR;


    /*
     * Test 4: Two parents, one child
     */
    /* Initialize shared task data struct */
    shared_task_data.max_ncalls = 3;
    OPA_store_int(&shared_task_data.ncalls, 0);

    /* Initialize task data struct */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        task_data[i].call_order = -1;

    /* Create first parent task */
    if(AE2create_task(engine, &task1, 0, NULL, 0, NULL, basic_task_worker,
            &task_data[0], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Create second parent task */
    if(AE2create_task(engine, &task2, 0, NULL, 0, NULL, basic_task_worker,
            &task_data[1], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Create child task */
    parent_task[0] = task1;
    parent_task[1] = task2;
    if(AE2create_task(engine, &task3, 0, NULL, 2, parent_task, basic_task_worker,
            &task_data[2], NULL) != AE2_SUCCEED)
        TEST_ERROR;

    /* Wait for tasks to complete */
    if(AE2wait(task3) < 0)
        TEST_ERROR;

    /* Verify results */
    for(i = 0; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].failed > 0)
            TEST_ERROR;
    if(task_data[2].call_order == 0)
        TEST_ERROR;
    for(i = 3; i < (sizeof(task_data) / sizeof(task_data[0])); i++)
        if(task_data[i].call_order != -1)
            TEST_ERROR;
    if(OPA_load_int(&shared_task_data.ncalls) != 3)
        TEST_ERROR;

    /* Close tasks */
    if(AE2finish(task1) != AE2_SUCCEED)
        TEST_ERROR;
    if(AE2finish(task2) != AE2_SUCCEED)
        TEST_ERROR;
    if(AE2finish(task3) != AE2_SUCCEED)
        TEST_ERROR;


    /*
     * Close
     */
    /* Terminate engine */
    if(AE2terminate_engine(engine, TRUE) != AE2_SUCCEED)
        TEST_ERROR;

    PASSED();
    return 0;

error:
    (void)AE2terminate_engine(engine, FALSE);

    return 1;
} /* end test_sufficient() */


int
main(int argc, char **argv)
{
    int i, j;
    int nerrors = 0;

    for(j = 0; j < 10; j++)
    /* Loop over number of threads */
    for(i = 0; i < (sizeof(num_threads_g) / sizeof(num_threads_g[0])); i++) {
        printf("----Testing with %d threads----\n", (int)num_threads_g[i]);

        /* The tests */
        nerrors += test_simple(num_threads_g[i]);
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

