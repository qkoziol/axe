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
#include "AE2threadpool.h"


/*
 * Local functions
 */
static void AE2_init(void);


/*
 * Local variables
 */
pthread_once_t AE2_init_once_g = PTHREAD_ONCE_INIT;
AE2_error_t AE2_init_status_g = AE2_SUCCEED;


AE2_error_t
AE2_engine_create(size_t num_threads, AE2_engine_int_t **engine/*out*/)
{
    AE2_error_t ret_value = AE2_SUCCEED;

    /* Call initialization routine, but only once */
    if(0 != pthread_once(&AE2_init_once_g, AE2_init))
        ERROR;

    /* Check if AE2_init failed */
    if(AE2_init_status_g != AE2_SUCCEED)
        ERROR;

    /* Allocate engine */
    if(NULL == (*engine = (AE2_engine_int_t *)malloc(sizeof(AE2_engine_int_t))))
        ERROR;

    /* Create schedule */
    if(AE2_schedule_create(num_threads, &(*engine)->schedule) != AE2_SUCCEED) {
        free(*engine);
        *engine = NULL;
        ERROR;
    } /* end if */
    assert((*engine)->schedule);

    /* Create thread pool */
    if(AE2_thread_pool_create(num_threads, &(*engine)->thread_pool) != AE2_SUCCEED) {
        (void)AE2_schedule_free((*engine)->schedule);
        free(*engine);
        *engine = NULL;
        ERROR;
    } /* end if */
    assert((*engine)->thread_pool);

done:
    return ret_value;
} /* end AE2_engine_create() */


AE2_error_t
AE2_engine_free(AE2_engine_int_t *engine)
{
    AE2_error_t ret_value = AE2_SUCCEED;

    /* Mark all tasks as canceled */
    AE2_schedule_cancel_all(engine->schedule);

    /* Free thread pool (will wait for all threads to finish) */
    if(AE2_thread_pool_free(engine->thread_pool) != AE2_SUCCEED)
        ERROR;

    /* Free schedule */
    if(AE2_schedule_free(engine->schedule) != AE2_SUCCEED)
        ERROR;

    /* Free engine */
    free(engine);

done:
    return ret_value;
} /* end AE2terminate_engine() */


static void
AE2_init(void)
{
    /* Initialize OPA shared memory.  Because we are just running threads in one
     * process, the memory will always be symmetric.  Just set the base address
     * to 0.  No need to worry about atomicity as this thread should only be
     * called once, and noone else should change AE2_init_status_g */
    if(OPA_Shm_asymm_init((char *)0) != 0)
        AE2_init_status_g = AE2_FAIL;

    return;
} /* end AE2_init() */

