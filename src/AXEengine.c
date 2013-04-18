/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "AXEengine.h"
#include "AXEschedule.h"
#include "AXEthreadpool.h"


/*
 * Local functions
 */
static void AXE_init(void);


/*
 * Local variables
 */
static pthread_once_t AXE_init_once_g = PTHREAD_ONCE_INIT;  /* Handle to execute AXE_init() exactly once in a process */
static AXE_error_t AXE_init_status_g = AXE_SUCCEED;         /* Return value of AXE_init() */


/*-------------------------------------------------------------------------
 * Function:    AXE_engine_create
 *
 * Purpose:     Internal routine to create an engine.  Allocates engine
 *              struct and calls AXE_schedule_create() and
 *              AXE_schedule_free().
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
AXE_engine_create(size_t num_threads, AXE_engine_int_t **engine/*out*/)
{
    AXE_error_t ret_value = AXE_SUCCEED;

    /* Call initialization routine, but only once */
    if(0 != pthread_once(&AXE_init_once_g, AXE_init))
        ERROR;

    /* Check if AXE_init failed */
    if(AXE_init_status_g != AXE_SUCCEED)
        ERROR;

    /* Allocate engine */
    if(NULL == (*engine = (AXE_engine_int_t *)malloc(sizeof(AXE_engine_int_t))))
        ERROR;

    /* Create schedule */
    if(AXE_schedule_create(&(*engine)->schedule) != AXE_SUCCEED) {
        free(*engine);
        *engine = NULL;
        ERROR;
    } /* end if */
    assert((*engine)->schedule);

    /* Create thread pool */
    if(AXE_thread_pool_create(num_threads, &(*engine)->thread_pool) != AXE_SUCCEED) {
        (void)AXE_schedule_free((*engine)->schedule);
        free(*engine);
        *engine = NULL;
        ERROR;
    } /* end if */
    assert((*engine)->thread_pool);

done:
    return ret_value;
} /* end AXE_engine_create() */


/*-------------------------------------------------------------------------
 * Function:    AXE_engine_free
 *
 * Purpose:     Frees an engine and all memory associated with it.
 *              Immediately cancels all tasks, then calls
 *              AXE_thread_pool_free() (which blocks until all currently
 *              running tasks complete) and AXE_schedule_free() before
 *              freeing the engine struct.
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
AXE_engine_free(AXE_engine_int_t *engine)
{
    AXE_error_t ret_value = AXE_SUCCEED;

#ifdef AXE_DEBUG_NTASKS
    printf("engine: %p\n", engine);
#endif /* AXE_DEBUG_NTASKS */

    /* Mark all tasks as canceled */
    if(AXE_schedule_cancel_all(engine->schedule, NULL) != AXE_SUCCEED)
        ERROR;

    /* Free thread pool (will wait for all threads to finish) */
    if(AXE_thread_pool_free(engine->thread_pool) != AXE_SUCCEED)
        ERROR;

    /* Free schedule */
    if(AXE_schedule_free(engine->schedule) != AXE_SUCCEED)
        ERROR;

    /* Free engine */
#ifndef NDEBUG
    memset(engine, 0, sizeof(*engine));
#endif /* NDEBUG */
    free(engine);

done:
    return ret_value;
} /* end AXE_engine_free() */


/*-------------------------------------------------------------------------
 * Function:    AXE_init
 *
 * Purpose:     Performs any one-time initialization on the library.
 *              Right now only calls OPA_Shm_asymm_init(), which is not
 *              necessary until an engine is created, so this function is
 *              only launched by AXE_engine_create().
 *
 * Return:      Success: AXE_SUCCEED
 *              Failure: AXE_FAIL
 *
 * Programmer:  Neil Fortner
 *              February-March, 2013
 *
 *-------------------------------------------------------------------------
 */
static void
AXE_init(void)
{
    /* Initialize OPA shared memory.  Because we are just running threads in one
     * process, the memory will always be symmetric.  Just set the base address
     * to 0.  No need to worry about atomicity as this thread should only be
     * called once, and noone else should change AXE_init_status_g */
    if(OPA_Shm_asymm_init((char *)0) != 0)
        AXE_init_status_g = AXE_FAIL;

    return;
} /* end AXE_init() */

