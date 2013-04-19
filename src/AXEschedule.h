/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef AXE_SCHEDULE_H_INCLUDED
#define AXE_SCHEDULE_H_INCLUDED

#include "AXEprivate.h"
#include "AXEtask.h"


/*
 * Typedefs
 */
typedef struct AXE_schedule_t AXE_schedule_t;


/*
 * Functions
 */
AXE_error_t AXE_schedule_create(AXE_schedule_t **schedule/*out*/);
AXE_error_t AXE_schedule_add(AXE_task_int_t *task);
AXE_error_t AXE_schedule_add_barrier(AXE_task_int_t *task);
AXE_error_t AXE_schedule_finish(AXE_task_int_t **task/*in,out*/);
AXE_error_t AXE_schedule_wait_all(AXE_schedule_t *schedule);
AXE_error_t AXE_schedule_cancel(AXE_task_int_t *task,
    AXE_remove_status_t *remove_status, _Bool have_task_mutex);
AXE_error_t AXE_schedule_cancel_all(AXE_schedule_t *schedule,
    AXE_id_table_t *id_table, AXE_remove_status_t *remove_status);
AXE_error_t AXE_schedule_remove_from_list(AXE_task_int_t *task);
void AXE_schedule_closing(AXE_schedule_t *schedule);
AXE_error_t AXE_schedule_free(AXE_schedule_t *schedule);


#endif /* AXE_SCHEDULE_H_INCLUDED */

