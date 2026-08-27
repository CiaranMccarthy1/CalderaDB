#ifndef CALDERADB_COLD_TIER_H
#define CALDERADB_COLD_TIER_H

#include <stdbool.h>
#include <stddef.h>
#include "calderadb/core/types.h"
#include "calderadb/util/hashTable.h"

/* Sync policies */
typedef enum {
    SYNC_ALWAYS,
    SYNC_EVERYSEC,
    SYNC_NO
} sync_policy_t;

/* Opaque type */
typedef struct cold_tier cold_tier_t;

/* Create/destroy */
cold_tier_t* cold_tier_create(const char* data_dir, sync_policy_t sync_policy);
void cold_tier_destroy(cold_tier_t* tier);

/* Core operations */
bool cold_tier_append(cold_tier_t* tier, document_t* doc);
document_t* cold_tier_read(cold_tier_t* tier, const doc_id_t* id);
bool cold_tier_mark_deleted(cold_tier_t* tier, const doc_id_t* id);
bool cold_tier_compact(cold_tier_t* tier);

/* Statistics and monitoring */
size_t cold_tier_total_bytes(const cold_tier_t* tier);
size_t cold_tier_doc_count(const cold_tier_t* tier);
sync_policy_t cold_tier_sync_policy(const cold_tier_t* tier);
uint64_t cold_tier_unsynced_bytes(const cold_tier_t* tier);
uint64_t cold_tier_last_sync_time(const cold_tier_t* tier);

/* Recovery */
bool cold_tier_recover(cold_tier_t* tier);

#endif /* CALDERADB_COLD_TIER_H */