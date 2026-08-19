#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "calderadb/engine/engine.h"
#include "calderadb/core/types.h"

#define NUM_DOCS 50000

double get_time_diff_sec(struct timespec* start, struct timespec* end) {
    return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

int main() {
    printf("Starting throughput benchmark...\n");
    system("rm -rf /tmp/calderadb_bench_tp");
    
    // 1MB hot tier
    calderadb_engine_t* engine = engine_create(1024 * 1024, "/tmp/calderadb_bench_tp");
    assert(engine != NULL);
    
    char** keys = malloc(NUM_DOCS * sizeof(char*));
    for (int i = 0; i < NUM_DOCS; i++) {
        keys[i] = malloc(32);
        snprintf(keys[i], 32, "key_%d", i);
    }
    
    // Make payload slightly large to force hot/cold tier interaction (e.g., 100 bytes)
    char val[100];
    memset(val, 'A', sizeof(val));
    val[sizeof(val) - 1] = '\0';
    size_t val_len = strlen(val);
    
    printf("Inserting %d docs...\n", NUM_DOCS);
    for (int i = 0; i < NUM_DOCS; i++) {
        engine_set(engine, keys[i], (const uint8_t*)val, val_len);
    }
    
    printf("Reading %d random docs...\n", NUM_DOCS);
    srand(42); // fixed seed for reproducibility
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < NUM_DOCS; i++) {
        int r = rand() % NUM_DOCS;
        document_t* doc = engine_get(engine, keys[r]);
        (void)doc; // unused
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double read_time = get_time_diff_sec(&start, &end);
    printf("Total read ops/sec: %.2f\n", NUM_DOCS / read_time);
    
    engine_stats_t stats = engine_stats(engine);
    
    double hot_hit_rate = (stats.total_gets > 0) ? (double)stats.hot_hits / stats.total_gets * 100.0 : 0.0;
    double cold_hit_rate = (stats.total_gets > 0) ? (double)stats.cold_hits / stats.total_gets * 100.0 : 0.0;
    
    printf("Hot hit rate: %.2f%%\n", hot_hit_rate);
    printf("Cold hit rate: %.2f%%\n", cold_hit_rate);
    
    engine_destroy(engine);
    for (int i = 0; i < NUM_DOCS; i++) {
        free(keys[i]);
    }
    free(keys);
    
    return 0;
}
