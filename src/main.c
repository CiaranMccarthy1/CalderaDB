#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "calderadb/calderadb.h"

static volatile int running = 1;

void signal_handler(int sig) {
    (void)sig;
    printf("\nShutting down CalderaDB...\n");
    running = 0;
}

/* Thread-local buffer for responses to avoid malloc/free since server doesn't free */
static __thread char resp_buf[8192];

uint8_t* request_handler(void* ctx, const uint8_t* req, size_t req_len, size_t* resp_len) {
    calderadb_engine_t* engine = (calderadb_engine_t*)ctx;
    
    // Copy request to a null-terminated string for easy parsing
    char cmd_buf[4096];
    size_t copy_len = req_len < sizeof(cmd_buf) - 1 ? req_len : sizeof(cmd_buf) - 1;
    memcpy(cmd_buf, req, copy_len);
    cmd_buf[copy_len] = '\0';

    // Strip trailing \r\n
    for (int i = copy_len - 1; i >= 0; i--) {
        if (cmd_buf[i] == '\r' || cmd_buf[i] == '\n') {
            cmd_buf[i] = '\0';
        } else {
            break;
        }
    }

    if (strncmp(cmd_buf, "PING", 4) == 0) {
        snprintf(resp_buf, sizeof(resp_buf), "+PONG\r\n");
    } else if (strncmp(cmd_buf, "SET ", 4) == 0) {
        char* key = cmd_buf + 4;
        char* space = strchr(key, ' ');
        if (space) {
            *space = '\0';
            char* value = space + 1;
            if (engine_set(engine, key, (const uint8_t*)value, strlen(value))) {
                snprintf(resp_buf, sizeof(resp_buf), "+OK\r\n");
            } else {
                snprintf(resp_buf, sizeof(resp_buf), "-ERR internal error\r\n");
            }
        } else {
            snprintf(resp_buf, sizeof(resp_buf), "-ERR invalid SET syntax\r\n");
        }
    } else if (strncmp(cmd_buf, "GET ", 4) == 0) {
        char* key = cmd_buf + 4;
        document_t* doc = engine_get(engine, key);
        if (doc) {
            snprintf(resp_buf, sizeof(resp_buf), "+%.*s\r\n", (int)doc->payload.len, doc->payload.data);
            // Wait, we need to free doc? engine_get returns a pointer to the doc in the hot tier.
            // Actually, in hotTier, it returns a pointer without transferring ownership.
        } else {
            snprintf(resp_buf, sizeof(resp_buf), "-NOT_FOUND\r\n");
        }
    } else if (strncmp(cmd_buf, "DEL ", 4) == 0) {
        char* key = cmd_buf + 4;
        if (engine_del(engine, key)) {
            snprintf(resp_buf, sizeof(resp_buf), "+OK\r\n");
        } else {
            snprintf(resp_buf, sizeof(resp_buf), "-NOT_FOUND\r\n");
        }
    } else if (strncmp(cmd_buf, "STATS", 5) == 0) {
        engine_stats_t s = engine_stats(engine);
        hot_tier_t* hot = engine_hot_tier(engine);
        cold_tier_t* cold = engine_cold_tier(engine);
        
        sync_policy_t pol = cold_tier_sync_policy(cold);
        const char* pol_str = config_sync_policy_to_string(pol);
        uint64_t unsynced = cold_tier_unsynced_bytes(cold);
        uint64_t last_sync = cold_tier_last_sync_time(cold);
        
        snprintf(resp_buf, sizeof(resp_buf), 
            "+gets:%zu sets:%zu dels:%zu hot_hits:%zu cold_hits:%zu misses:%zu hot_docs:%zu hot_bytes:%zu cold_docs:%zu cold_bytes:%zu sync_policy:%s unsynced_bytes:%lu last_sync:%lu\r\n",
            s.total_gets, s.total_sets, s.total_dels, s.hot_hits, s.cold_hits, s.misses,
            hot_tier_doc_count(hot), hot_tier_used_bytes(hot),
            cold_tier_doc_count(cold), cold_tier_total_bytes(cold),
            pol_str, (unsigned long)unsynced, (unsigned long)last_sync);
    } else {
        snprintf(resp_buf, sizeof(resp_buf), "-ERR unknown command\r\n");
    }

    *resp_len = strlen(resp_buf);
    return (uint8_t*)resp_buf;
}

static void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  --port <port>                 TCP port for incoming client connections (default: 9090)\n");
    printf("  --data-dir <path>             Filesystem directory for cold tier storage (default: /tmp/calderadb)\n");
    printf("  --hot-capacity <MB>           Maximum RAM capacity allocated for hot tier (in MB) (default: 1024)\n");
    printf("  --sync-policy <always|everysec|no>  Durability sync policy for cold tier (default: everysec)\n");
    printf("  --sync-policy=<val>           Durability sync policy for cold tier\n");
    printf("  --config <path>               Load configuration file\n");
    printf("  --help, -h                    Show this help message\n");
}

int main(int argc, char** argv) {
    printf("CalderaDB v0.1.0 - Access-Aware NoSQL Database\n");
    printf("Built: %s %s\n", __DATE__, __TIME__);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    calderadb_config_t config;
    config_init_default(&config);
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_load(argv[++i], &config);
        } else if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
            strncpy(config.data_dir, argv[++i], sizeof(config.data_dir) - 1);
            config.data_dir[sizeof(config.data_dir) - 1] = '\0';
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config.port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--hot-capacity") == 0 && i + 1 < argc) {
            config.hot_capacity_bytes = (size_t)atoi(argv[++i]) * 1024 * 1024;
        } else if (strcmp(argv[i], "--sync-policy") == 0 && i + 1 < argc) {
            config.sync_policy = config_parse_sync_policy(argv[++i]);
        } else if (strncmp(argv[i], "--sync-policy=", 14) == 0) {
            config.sync_policy = config_parse_sync_policy(argv[i] + 14);
        }
    }
    
    printf("Data directory: %s\n", config.data_dir);
    printf("Port: %d\n", config.port);
    printf("Hot tier capacity: %zu MB\n", config.hot_capacity_bytes / (1024 * 1024));
    printf("Sync policy: %s\n", config_sync_policy_to_string(config.sync_policy));
    
    calderadb_engine_t* engine = engine_create_with_sync_policy(config.hot_capacity_bytes, config.data_dir, config.sync_policy);
    if (!engine) {
        fprintf(stderr, "Failed to create engine\n");
        return 1;
    }
    
    /* Create TCP server */
    tcp_server_t* server = tcp_server_create(config.port, 128, request_handler, engine);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        engine_destroy(engine);
        return 1;
    }
    
    printf("Server running on port %d\n", config.port);
    printf("Press Ctrl+C to stop\n");
    
    /* Run server (blocking) */
    tcp_server_run(server);
    
    /* Cleanup */
    tcp_server_destroy(server);
    engine_destroy(engine);
    
    printf("Shutdown complete.\n");
    return 0;
}