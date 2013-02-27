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
struct AXE_task_int_t {
    /* Fields used for the callback */
    AXE_task_op_t           op;
    size_t                  num_necessary_parents;
    AXE_task_int_t          **necessary_parents;
    size_t                  num_sufficient_parents;
    AXE_task_int_t          **sufficient_parents;
    void                    *op_data;

    /* Internal fields */
    OPA_Queue_element_hdr_t scheduled_queue_hdr;
    AXE_engine_int_t        *engine;
    AXE_task_free_op_data_t free_op_data;
    pthread_mutex_t         task_mutex;
    pthread_cond_t          wait_cond;
    pthread_mutex_t         wait_mutex;
    OPA_int_t               status;
    OPA_int_t               rc;
    OPA_int_t               sufficient_complete;
    OPA_int_t               num_conditions_complete;     /* Number of needed conditions = num_necessary_parents + 2 (1 for all sufficient parents (even 0), 1 for initialization) */
    size_t                  num_necessary_children;
    size_t                  necessary_children_nalloc;
    AXE_task_int_t          **necessary_children;
    size_t                  num_sufficient_children;
    size_t                  sufficient_children_nalloc;
    AXE_task_int_t          **sufficient_children;
    AXE_task_int_t          *task_list_next;
    AXE_task_int_t          *task_list_prev;
};


/*
 * Functions
 */
void AXE_task_incr_ref(AXE_task_int_t *task);
void AXE_task_decr_ref(AXE_task_int_t *task);
AXE_error_t AXE_task_create(AXE_engine_int_t *engine,
    AXE_task_int_t **task/*out*/, size_t num_necessary_parents,
    AXE_task_int_t **necessary_parents, size_t num_sufficient_parents,
    AXE_task_int_t **sufficient_parents, AXE_task_op_t op, void *op_data,
    AXE_task_free_op_data_t free_op_data);
void AXE_task_get_op_data(AXE_task_int_t *task, void **op_data/*out*/);
void AXE_task_get_status(AXE_task_int_t *task, AXE_status_t *status/*out*/);
AXE_error_t AXE_task_worker(void *_task);
AXE_error_t AXE_task_wait(AXE_task_int_t *task);
AXE_error_t AXE_task_free(AXE_task_int_t *task);


#endif /* AXE_TASK_H_INCLUDED */

