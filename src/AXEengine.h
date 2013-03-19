/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef AXE_ENGINE_H_INCLUDED
#define AXE_ENGINE_H_INCLUDED

#include "AXEprivate.h"
#include "AXEschedule.h"
#include "AXEthreadpool.h"


/*
 * Typedefs
 */
/* An engine consists of a schedule and a thread pool */
struct AXE_engine_int_t {
    AXE_schedule_t          *schedule;
    AXE_thread_pool_t       *thread_pool;
};


/*
 * Functions
 */
AXE_error_t AXE_engine_create(size_t num_threads,
    AXE_engine_int_t **engine/*out*/);
AXE_error_t AXE_engine_free(AXE_engine_int_t *engine);


#endif /* AXE_ENGINE_H_INCLUDED */

