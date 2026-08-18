#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "calderadb/engine/engine.h"
#include "calderadb/core/types.h"

#define NUM_DOCS 10000

int cmp_double(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

double get_time_diff_sec(struct timespec* start, struct timespec* end) {
    return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

int main() {
    printf("Starting latency benchmark...\n");
    system("rm -rf /tmp/calderadb_bench_lat");
    
    calderadb_engine_t* engine = engine_create(1024 * 1024 * 10, "/tmp/calderadb_bench_lat");
    assert(engine != NULL);
    
    char** keys = malloc(NUM_DOCS * sizeof(char*));
    for (int i = 0; i < NUM_DOCS; i++) {
        keys[i] = malloc(32);
        snprintf(keys[i], 32, "key_%d", i);
    }
    
    const char* val = "benchmark_value_data";
    size_t val_len = strlen(val);
    
    struct timespec start, end;
    
    // Insert benchmark
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < NUM_DOCS; i++) {
        engine_set(engine, keys[i], (const uint8_t*)val, val_len);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double insert_time = get_time_diff_sec(&start, &end);
    printf("Insert ops/sec: %.2f\n", NUM_DOCS / insert_time);
    
    // Read benchmark
    double* latencies = malloc(NUM_DOCS * sizeof(double));
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < NUM_DOCS; i++) {
        struct timespec t1, t2;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        
        document_t* doc = engine_get(engine, keys[i]);
        (void)doc;
        
        clock_gettime(CLOCK_MONOTONIC, &t2);
        latencies[i] = get_time_diff_sec(&t1, &t2);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double read_time = get_time_diff_sec(&start, &end);
    printf("Read ops/sec: %.2f\n", NUM_DOCS / read_time);
    
    qsort(latencies, NUM_DOCS, sizeof(double), cmp_double);
    
    printf("p50 read latency: %.6f sec\n", latencies[(int)(NUM_DOCS * 0.50)]);
    printf("p95 read latency: %.6f sec\n", latencies[(int)(NUM_DOCS * 0.95)]);
    printf("p99 read latency: %.6f sec\n", latencies[(int)(NUM_DOCS * 0.99)]);
    
    engine_destroy(engine);
    for (int i = 0; i < NUM_DOCS; i++) {
        free(keys[i]);
    }
    free(keys);
    free(latencies);
    
    return 0;
}
