#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int port;
    size_t hot_capacity_bytes;
    uint32_t eviction_x;
    uint64_t eviction_y;
    char data_dir[256];
} config_t;

bool config_load(const char* filename, config_t* cfg) {
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
        }
    }
    
    fclose(fp);
    return true;
}

void config_init_default(config_t* cfg) {
    cfg->port = 9090;
    cfg->hot_capacity_bytes = 512 * 1024 * 1024;
    cfg->eviction_x = 5;
    cfg->eviction_y = 60000;
    strcpy(cfg->data_dir, "/tmp/calderadb");
}