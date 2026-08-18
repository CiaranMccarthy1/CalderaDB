#ifndef CALDERADB_HOT_TIER_H
#define CALDERADB_HOT_TIER_H

#include <stdbool.h>
#include <stddef.h>
#include "calderadb/core/types.h"
#include "calderadb/util/hashTable.h"

/* Opaque type */
typedef struct hot_tier hot_tier_t;

/* Create/destroy */
hot_tier_t* hot_tier_create(size_t capacity_bytes);
void hot_tier_destroy(hot_tier_t* tier);

/* Core operations */
bool hot_tier_insert(hot_tier_t* tier, document_t* doc);
document_t* hot_tier_get(hot_tier_t* tier, const doc_id_t* id);
bool hot_tier_remove(hot_tier_t* tier, const doc_id_t* id);
document_t* hot_tier_evict_one(hot_tier_t* tier);

/* Statistics */
size_t hot_tier_used_bytes(const hot_tier_t* tier);
size_t hot_tier_doc_count(const hot_tier_t* tier);
size_t hot_tier_capacity_bytes(const hot_tier_t* tier);

/* Iteration */
typedef struct hot_tier_iter {
    ht_iter_t* iter;
} hot_tier_iter_t;

hot_tier_iter_t* hot_tier_iter_create(hot_tier_t* tier);
document_t* hot_tier_iter_next(hot_tier_iter_t* iter);
void hot_tier_iter_destroy(hot_tier_iter_t* iter);

#endif /* CALDERADB_HOT_TIER_H */