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

#ifndef AXE_SCHEDULE_H_INCLUDED
#define AXE_SCHEDULE_H_INCLUDED

#include "AXEprivate.h"


/*
 * Typedefs
 */
typedef struct AXE_schedule_t AXE_schedule_t;


/*
 * Functions
 */
AXE_error_t AXE_schedule_create(size_t num_threads,
    AXE_schedule_t **schedule/*out*/);
void AXE_schedule_worker_running(AXE_schedule_t *schedule);
AXE_error_t AXE_schedule_add(AXE_task_int_t *task);
AXE_error_t AXE_schedule_finish(AXE_task_int_t **task/*in,out*/);
AXE_error_t AXE_schedule_wait_all(AXE_schedule_t *schedule);
AXE_error_t AXE_schedule_cancel(AXE_task_int_t *task,
    AXE_remove_status_t *remove_status, _Bool have_task_mutex);
AXE_error_t AXE_schedule_cancel_all(AXE_schedule_t *schedule,
    AXE_remove_status_t *remove_status);
AXE_error_t AXE_schedule_remove_from_list(AXE_task_int_t *task);
AXE_error_t AXE_schedule_free(AXE_schedule_t *schedule);


#endif /* AXE_SCHEDULE_H_INCLUDED */

