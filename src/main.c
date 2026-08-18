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
        
        snprintf(resp_buf, sizeof(resp_buf), 
            "+gets:%zu sets:%zu dels:%zu hot_hits:%zu cold_hits:%zu misses:%zu hot_docs:%zu hot_bytes:%zu cold_docs:%zu cold_bytes:%zu\r\n",
            s.total_gets, s.total_sets, s.total_dels, s.hot_hits, s.cold_hits, s.misses,
            hot_tier_doc_count(hot), hot_tier_used_bytes(hot),
            cold_tier_doc_count(cold), cold_tier_total_bytes(cold));
    } else {
        snprintf(resp_buf, sizeof(resp_buf), "-ERR unknown command\r\n");
    }

    *resp_len = strlen(resp_buf);
    return (uint8_t*)resp_buf;
}

int main(int argc, char** argv) {
    printf("CalderaDB v0.1.0 - Access-Aware NoSQL Database\n");
    printf("Built: %s %s\n", __DATE__, __TIME__);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Parse arguments */
    const char* data_dir = "/tmp/calderadb";
    int port = 9090;
    size_t hot_capacity = 512 * 1024 * 1024; /* 512 MB */
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--hot-capacity") == 0 && i + 1 < argc) {
            hot_capacity = (size_t)atoi(argv[++i]) * 1024 * 1024;
        }
    }
    
    printf("Data directory: %s\n", data_dir);
    printf("Port: %d\n", port);
    printf("Hot tier capacity: %zu MB\n", hot_capacity / (1024 * 1024));
    
    calderadb_engine_t* engine = engine_create(hot_capacity, data_dir);
    if (!engine) {
        fprintf(stderr, "Failed to create engine\n");
        return 1;
    }
    
    /* Create TCP server */
    tcp_server_t* server = tcp_server_create(port, 128, request_handler, engine);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        engine_destroy(engine);
        return 1;
    }
    
    printf("Server running on port %d\n", port);
    printf("Press Ctrl+C to stop\n");
    
    /* Run server (blocking) */
    tcp_server_run(server);
    
    /* Cleanup */
    tcp_server_destroy(server);
    engine_destroy(engine);
    
    printf("Shutdown complete.\n");
    return 0;
}