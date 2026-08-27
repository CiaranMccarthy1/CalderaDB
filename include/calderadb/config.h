#ifndef CALDERADB_CONFIG_H
#define CALDERADB_CONFIG_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "calderadb/cold/coldTier.h"

typedef struct {
    int port;
    size_t hot_capacity_bytes;
    uint32_t eviction_x;
    uint64_t eviction_y;
    char data_dir[256];
    sync_policy_t sync_policy;
} calderadb_config_t;

typedef calderadb_config_t config_t;

void config_init_default(calderadb_config_t* cfg);
bool config_load(const char* filename, calderadb_config_t* cfg);
sync_policy_t config_parse_sync_policy(const char* str);
const char* config_sync_policy_to_string(sync_policy_t policy);

#endif /* CALDERADB_CONFIG_H */
