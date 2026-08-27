#include "calderadb/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

sync_policy_t config_parse_sync_policy(const char* str) {
    if (!str) return SYNC_EVERYSEC;
    if (strcmp(str, "always") == 0) {
        return SYNC_ALWAYS;
    } else if (strcmp(str, "everysec") == 0) {
        return SYNC_EVERYSEC;
    } else if (strcmp(str, "no") == 0) {
        return SYNC_NO;
    }
    return SYNC_EVERYSEC;
}

const char* config_sync_policy_to_string(sync_policy_t policy) {
    switch (policy) {
        case SYNC_ALWAYS: return "always";
        case SYNC_EVERYSEC: return "everysec";
        case SYNC_NO: return "no";
        default: return "unknown";
    }
}

bool config_load(const char* filename, calderadb_config_t* cfg) {
    FILE* fp = fopen(filename, "r");
    if (!fp) return false;
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char* newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        char* eq = strchr(line, '=');
        if (!eq) continue;
        
        *eq = '\0';
        char* key = line;
        char* value = eq + 1;
        
        /* Trim whitespace */
        while (*key == ' ') key++;
        while (*value == ' ') value++;
        
        if (strcmp(key, "port") == 0) {
            cfg->port = atoi(value);
        } else if (strcmp(key, "hot_capacity_mb") == 0) {
            cfg->hot_capacity_bytes = (size_t)atoi(value) * 1024 * 1024;
        } else if (strcmp(key, "eviction_x") == 0) {
            cfg->eviction_x = atoi(value);
        } else if (strcmp(key, "eviction_y") == 0) {
            cfg->eviction_y = atoi(value);
        } else if (strcmp(key, "data_dir") == 0) {
            strncpy(cfg->data_dir, value, sizeof(cfg->data_dir) - 1);
            cfg->data_dir[sizeof(cfg->data_dir) - 1] = '\0';
        } else if (strcmp(key, "sync_policy") == 0) {
            cfg->sync_policy = config_parse_sync_policy(value);
        }
    }
    
    fclose(fp);
    return true;
}

// default config values 
void config_init_default(calderadb_config_t* cfg) {
    cfg->port = 9090;
    cfg->hot_capacity_bytes = 1024 * 1024 * 1024;
    cfg->eviction_x = 5;
    cfg->eviction_y = 60000;
    strcpy(cfg->data_dir, "/tmp/calderadb");
    cfg->sync_policy = SYNC_EVERYSEC;
}