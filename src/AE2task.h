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

#ifndef AE2_TASK_H_INCLUDED
#define AE2_TASK_H_INCLUDED

#include "AE2private.h"


/*
 * Macros
 */
/* Initial allocation size for necessary and sufficient children arrays */
#define AE2_TASK_NCHILDREN_INIT 8


/*
 * Typedefs
 */
struct AE2_task_int_t {
    /* Fields used for the callback */
    AE2_task_op_t           op;
    size_t                  num_necessary_parents;
    AE2_task_int_t          **necessary_parents;
    size_t                  num_sufficient_parents;
    AE2_task_int_t          **sufficient_parents;
    void                    *op_data;

    /* Internal fields */
    OPA_Queue_element_hdr_t scheduled_queue_hdr;
    AE2_engine_int_t        *engine;
    AE2_task_free_op_data_t free_op_data;
    pthread_mutex_t         task_mutex;
    pthread_cond_t          wait_cond;
    pthread_mutex_t         wait_mutex;
    OPA_int_t               status;
    OPA_int_t               rc;
    OPA_int_t               sufficient_complete;
    OPA_int_t               num_conditions_complete;     /* Number of needed conditions = num_necessary_parents + 2 (1 for all sufficient parents (even 0), 1 for initialization) */
    size_t                  num_necessary_children;
    size_t                  necessary_children_nalloc;
    AE2_task_int_t          **necessary_children;
    size_t                  num_sufficient_children;
    size_t                  sufficient_children_nalloc;
    AE2_task_int_t          **sufficient_children;
    AE2_task_int_t          *task_list_next;
    AE2_task_int_t          *task_list_prev;
};


/*
 * Functions
 */
void AE2_task_incr_ref(AE2_task_int_t *task);
void AE2_task_decr_ref(AE2_task_int_t *task);
AE2_error_t AE2_task_create(AE2_engine_int_t *engine,
    AE2_task_int_t **task/*out*/, size_t num_necessary_parents,
    AE2_task_int_t **necessary_parents, size_t num_sufficient_parents,
    AE2_task_int_t **sufficient_parents, AE2_task_op_t op, void *op_data,
    AE2_task_free_op_data_t free_op_data);
void AE2_task_get_op_data(AE2_task_int_t *task, void **op_data/*out*/);
void AE2_task_get_status(AE2_task_int_t *task, AE2_status_t *status/*out*/);
AE2_error_t AE2_task_worker(void *_task);
AE2_error_t AE2_task_wait(AE2_task_int_t *task);
AE2_error_t AE2_task_free(AE2_task_int_t *task);


#endif /* AE2_TASK_H_INCLUDED */

