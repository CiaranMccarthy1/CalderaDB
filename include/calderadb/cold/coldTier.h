#ifndef CALDERADB_COLD_TIER_H
#define CALDERADB_COLD_TIER_H

#include <stdbool.h>
#include <stddef.h>
#include "calderadb/core/types.h"
#include "calderadb/util/hashTable.h"

/* Opaque type */
typedef struct cold_tier cold_tier_t;

/* Create/destroy */
cold_tier_t* cold_tier_create(const char* data_dir);
void cold_tier_destroy(cold_tier_t* tier);

/* Core operations */
bool cold_tier_append(cold_tier_t* tier, document_t* doc);
document_t* cold_tier_read(cold_tier_t* tier, const doc_id_t* id);
bool cold_tier_mark_deleted(cold_tier_t* tier, const doc_id_t* id);

/* Statistics */
size_t cold_tier_total_bytes(const cold_tier_t* tier);
size_t cold_tier_doc_count(const cold_tier_t* tier);

/* Recovery */
bool cold_tier_recover(cold_tier_t* tier);

#endif /* CALDERADB_COLD_TIER_H */