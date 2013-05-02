/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of AXE.  The full AXE copyright notice, including terms *
 * governing use, modification, and redistribution, is contained in the file *
 * COPYING at the root of the source code distribution tree.                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef AXE_ID_H_INCLUDED
#define AXE_ID_H_INCLUDED

#include "AXEprivate.h"


/*
 * Macros
 */
/* Default number of buckets for id tables */
#define AXE_ID_NUM_BUCKETS_DEF 10007

/* Default number of bucket mutexes for id tables */
#define AXE_ID_NUM_MUTEXES_DEF 503

/* Default minimum id for AXE_id_generate() */
#define AXE_ID_MIN_ID_DEF 0

/* Default maximum id for AXE_id_generate() */
#define AXE_ID_MAX_ID_DEF UINT64_MAX


/*
 * Typedefs
 */
/* id table structure */
typedef struct AXE_id_table_t AXE_id_table_t;

/* Type for ids in the id table */
typedef uint64_t AXE_id_t;

/* Callback function for AXE_id_iterate() and AXE_id_table_free() */
typedef AXE_error_t (*AXE_id_iterate_op_t)(void *obj, void *op_data);


/*
 * Functions
 */
AXE_error_t AXE_id_table_create(size_t num_buckets, size_t num_mutexes,
    AXE_id_t min_id, AXE_id_t max_id, AXE_id_table_t **id_table/*out*/);
AXE_error_t AXE_id_generate(AXE_id_table_t *id_table, AXE_id_t *id);
AXE_error_t AXE_id_insert(AXE_id_table_t *id_table, AXE_id_t id, void *obj);
AXE_error_t AXE_id_lookup(AXE_id_table_t *id_table, AXE_id_t id, void **obj);
AXE_error_t AXE_id_remove(AXE_id_table_t *id_table, AXE_id_t id);
AXE_error_t AXE_id_iterate(AXE_id_table_t *id_table, AXE_id_iterate_op_t op,
    void *op_data);
AXE_error_t AXE_id_table_free(AXE_id_table_t *id_table, AXE_id_iterate_op_t op,
    void *op_data);


#endif /* AXE_ID_H_INCLUDED */

