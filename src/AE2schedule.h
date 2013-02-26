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

#ifndef AE2_SCHEDULE_H_INCLUDED
#define AE2_SCHEDULE_H_INCLUDED

#include "AE2private.h"


/*
 * Typedefs
 */
typedef struct AE2_schedule_t AE2_schedule_t;


/*
 * Functions
 */
AE2_error_t AE2_schedule_create(size_t num_threads,
    AE2_schedule_t **schedule/*out*/);
void AE2_schedule_worker_running(AE2_schedule_t *schedule);
AE2_error_t AE2_schedule_add(AE2_task_int_t *task);
AE2_error_t AE2_schedule_finish(AE2_task_int_t **task/*in,out*/);
AE2_error_t AE2_schedule_wait_all(AE2_schedule_t *schedule);
void AE2_schedule_cancel_all(AE2_schedule_t *schedule);
AE2_error_t AE2_schedule_remove_task(AE2_task_int_t *task);
AE2_error_t AE2_schedule_free(AE2_schedule_t *schedule);


#endif /* AE2_SCHEDULE_H_INCLUDED */

