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

/* Global state */
static hot_tier_t* hot_tier = NULL;
static cold_tier_t* cold_tier = NULL;

uint8_t* request_handler(void* ctx, const uint8_t* req, size_t req_len, size_t* resp_len) {
    (void)ctx;
    
    /* Simple protocol: just echo "OK" for any request */
    static uint8_t response[] = "OK\n";
    *resp_len = 3;
    return (uint8_t*)response;
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
    
    /* Initialize tiers */
    hot_tier = hot_tier_create(hot_capacity);
    if (!hot_tier) {
        fprintf(stderr, "Failed to create hot tier\n");
        return 1;
    }
    
    cold_tier = cold_tier_create(data_dir);
    if (!cold_tier) {
        fprintf(stderr, "Failed to create cold tier\n");
        hot_tier_destroy(hot_tier);
        return 1;
    }
    
    /* Create TCP server */
    tcp_server_t* server = tcp_server_create(port, 128, request_handler, NULL);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        cold_tier_destroy(cold_tier);
        hot_tier_destroy(hot_tier);
        return 1;
    }
    
    printf("Server running on port %d\n", port);
    printf("Press Ctrl+C to stop\n");
    
    /* Run server (blocking) */
    tcp_server_run(server);
    
    /* Cleanup */
    tcp_server_destroy(server);
    cold_tier_destroy(cold_tier);
    hot_tier_destroy(hot_tier);
    
    printf("Shutdown complete.\n");
    return 0;
}