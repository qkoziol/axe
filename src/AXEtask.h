/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef AXE_TASK_H_INCLUDED
#define AXE_TASK_H_INCLUDED

#include "AXEprivate.h"


/*
 * Macros
 */
/* Initial allocation size for necessary and sufficient children arrays */
#define AXE_TASK_NCHILDREN_INIT 8


/*
 * Typedefs
 */
/* Task structure.  Includes fields used by the scheduler, so it must be present
 * in a header visible to the scheduler. */
struct AXE_task_int_t {
    /* Fields used for the callback */
    AXE_task_op_t           op;                     /* Client task callback function */
    size_t                  num_necessary_parents;  /* Number of necessary parent tasks */
    AXE_task_int_t          **necessary_parents;    /* Array of necessary parent tasks */
    size_t                  num_sufficient_parents; /* Number of sufficient parent tasks */
    AXE_task_int_t          **sufficient_parents;   /* Array of sufficient parent tasks */
    void                    *op_data;               /* Client task callback data pointer */

    /* Internal fields */
    OPA_Queue_element_hdr_t scheduled_queue_hdr;    /* Header for insertion into the schedule's "scheduled_queue" */
    AXE_engine_int_t        *engine;                /* Pointer to the engine this task resides in */
    AXE_task_free_op_data_t free_op_data;           /* Callback provided to free op_data */
    pthread_mutex_t         task_mutex;             /* Mutex locked for making certain changes to this struct */
    pthread_cond_t          wait_cond;              /* Condition variable for signaling threads waiting for this task to complete */
    OPA_int_t               status;                 /* Status of this task */
    OPA_int_t               rc;                     /* Reference count of this task */
    OPA_int_t               sufficient_complete;    /* Boolean variable indicating if all sufficient parents are complete */
    OPA_int_t               num_conditions_complete; /* Number of conditions complete.  Number of conditions needed in order to be scheduled = num_necessary_parents + 2 (1 for all sufficient parents (even 0), 1 for initialization). */
    size_t                  num_necessary_children; /* Number of necessary child tasks */
    size_t                  necessary_children_nalloc; /* Size of necessary_children in elements */
    AXE_task_int_t          **necessary_children;   /* Array of necessary child tasks */
    size_t                  num_sufficient_children; /* Number of sufficient child tasks */
    size_t                  sufficient_children_nalloc; /* size of sufficient_children in elements */
    AXE_task_int_t          **sufficient_children;  /* Array of sufficient child tasks */
    AXE_task_int_t          *task_list_next;        /* Next task in task list */
    AXE_task_int_t          *task_list_prev;        /* Previous task in task list */
    AXE_task_int_t          *free_list_next;        /* Next task in free list.  Should be NULL unless rc has dropped to 0 and it is about to be freed by AXE_schedule_finish(). */
};


/*
 * Functions
 */
void AXE_task_incr_ref(AXE_task_int_t *task);
AXE_error_t AXE_task_decr_ref(AXE_task_int_t *task, AXE_task_int_t **free_ptr);
AXE_error_t AXE_task_create(AXE_engine_int_t *engine,
    AXE_task_int_t **task/*out*/, size_t num_necessary_parents,
    AXE_task_int_t **necessary_parents, size_t num_sufficient_parents,
    AXE_task_int_t **sufficient_parents, AXE_task_op_t op, void *op_data,
    AXE_task_free_op_data_t free_op_data);
AXE_error_t AXE_task_create_barrier(AXE_engine_int_t *engine,
    AXE_task_int_t **task/*out*/, AXE_task_op_t op, void *op_data,
    AXE_task_free_op_data_t free_op_data);
void AXE_task_get_op_data(AXE_task_int_t *task, void **op_data/*out*/);
void AXE_task_get_status(AXE_task_int_t *task, AXE_status_t *status/*out*/);
AXE_error_t AXE_task_worker(void *_task);
AXE_error_t AXE_task_wait(AXE_task_int_t *task);
AXE_error_t AXE_task_cancel_leaf(AXE_task_int_t *task,
    AXE_remove_status_t *remove_status);
AXE_error_t AXE_task_free(AXE_task_int_t *task);


#endif /* AXE_TASK_H_INCLUDED */

