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
#include "AXEid.h"
#include "AXEschedule.h"
#include "AXEthreadpool.h"


/*
 * Typedefs
 */
/* An engine consists of a schedule, an id table, and a thread pool */
struct AXE_engine_int_t {
    AXE_schedule_t          *schedule;
    AXE_id_table_t          *id_table;
    AXE_thread_pool_t       *thread_pool;
    _Bool                   exclude_close;      /* Whether to fail if tasks still exist when closing.  Used for testing. */
};


/*
 * Functions
 */
AXE_error_t AXE_engine_create(AXE_engine_int_t **engine/*out*/,
    const AXE_engine_attr_t *attr);
AXE_error_t AXE_engine_free(AXE_engine_int_t *engine);


/*
 * Global variables
 */
extern const AXE_engine_attr_t AXE_engine_attr_def_g;


#endif /* AXE_ENGINE_H_INCLUDED */

