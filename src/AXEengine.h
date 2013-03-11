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

