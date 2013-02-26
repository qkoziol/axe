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

#ifndef AE2_ENGINE_H_INCLUDED
#define AE2_ENGINE_H_INCLUDED

#include "AE2private.h"
#include "AE2schedule.h"
#include "AE2threadpool.h"


/*
 * Typedefs
 */
struct AE2_engine_int_t {
    AE2_schedule_t          *schedule;
    AE2_thread_pool_t       *thread_pool;
};


/*
 * Functions
 */
AE2_error_t AE2_engine_create(size_t num_threads,
    AE2_engine_int_t **engine/*out*/);
AE2_error_t AE2_engine_free(AE2_engine_int_t *engine);


#endif /* AE2_ENGINE_H_INCLUDED */

