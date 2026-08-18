#ifndef CALDERADB_ENGINE_H
#define CALDERADB_ENGINE_H

#include "calderadb/calderadb.h"
#include <pthread.h>

typedef struct calderadb_engine calderadb_engine_t;

typedef struct {
    size_t total_gets;
    size_t total_sets;
    size_t total_dels;
    size_t hot_hits;
    size_t cold_hits;
    size_t misses;
} engine_stats_t;

calderadb_engine_t* engine_create(size_t hot_capacity, const char* data_dir);
void engine_destroy(calderadb_engine_t* engine);

// Core operations
document_t* engine_get(calderadb_engine_t* engine, const char* key);
bool engine_set(calderadb_engine_t* engine, const char* key, const uint8_t* value, size_t value_len);
bool engine_del(calderadb_engine_t* engine, const char* key);

engine_stats_t engine_stats(calderadb_engine_t* engine);
hot_tier_t* engine_hot_tier(calderadb_engine_t* engine);
cold_tier_t* engine_cold_tier(calderadb_engine_t* engine);

#endif
