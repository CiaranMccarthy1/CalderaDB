#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "calderadb/engine/engine.h"
#include "calderadb/core/types.h"

#define NUM_DOCS 10000

int cmp_uint64(const void* a, const void* b) {
    uint64_t ua = *(const uint64_t*)a;
    uint64_t ub = *(const uint64_t*)b;
    if (ua < ub) return -1;
    if (ua > ub) return 1;
    return 0;
}

uint64_t timespec_to_ns(struct timespec* ts) {
    return ts->tv_sec * (uint64_t)1e9 + ts->tv_nsec;
}

void print_latency_stats(const char* label, uint64_t* latencies, int count) {
    qsort(latencies, count, sizeof(uint64_t), cmp_uint64);
    
    uint64_t sum = 0;
    for (int i = 0; i < count; i++) {
        sum += latencies[i];
    }
    
    printf("\n%s:\n", label);
    printf("  p50:   %.3f us\n", latencies[(int)(count * 0.50)] / 1e3);
    printf("  p95:   %.3f us\n", latencies[(int)(count * 0.95)] / 1e3);
    printf("  p99:   %.3f us\n", latencies[(int)(count * 0.99)] / 1e3);
    printf("  p99.9: %.3f us\n", latencies[(int)(count * 0.999)] / 1e3);
    printf("  max:   %.3f us\n", latencies[count - 1] / 1e3);
    printf("  avg:   %.3f us\n", (double)sum / count / 1e3);
}

int main() {
    printf("========== CalderaDB Latency Benchmark ==========\n");
    printf("Documents: %d, Hot tier: 10MB\n\n", NUM_DOCS);
    
    system("rm -rf /tmp/calderadb_bench_lat");
    
    calderadb_engine_t* engine = engine_create(1024 * 1024 * 1024, "/tmp/calderadb_bench_lat");
    assert(engine != NULL);
    
    // Pre-allocate keys
    char** keys = malloc(NUM_DOCS * sizeof(char*));
    for (int i = 0; i < NUM_DOCS; i++) {
        keys[i] = malloc(32);
        snprintf(keys[i], 32, "key_%d", i);
    }
    
    const char* val = "benchmark_value_data";
    size_t val_len = strlen(val);
    
    struct timespec start, end;
    
    // ===== INSERT PHASE =====
    printf("INSERT PHASE\n");
    printf("-----------\n");
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < NUM_DOCS; i++) {
        engine_set(engine, keys[i], (const uint8_t*)val, val_len);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    uint64_t insert_ns = timespec_to_ns(&end) - timespec_to_ns(&start);
    printf("Total time: %.2f ms\n", insert_ns / 1e6);
    printf("Throughput: %.0f ops/sec\n", NUM_DOCS / (insert_ns / 1e9));
    
    // Check tier state after inserts
    hot_tier_t* hot = engine_hot_tier(engine);
    cold_tier_t* cold = engine_cold_tier(engine);
    engine_stats_t stats = engine_stats(engine);
    
    printf("Hot tier: %zu docs, %.2f MB / %.2f MB (%.1f%% full)\n",
           hot_tier_doc_count(hot),
           hot_tier_used_bytes(hot) / 1e6,
           hot_tier_capacity_bytes(hot) / 1e6,
           100.0 * hot_tier_used_bytes(hot) / hot_tier_capacity_bytes(hot));
    printf("Cold tier: %zu docs, %.2f MB\n",
           cold_tier_doc_count(cold),
           cold_tier_total_bytes(cold) / 1e6);
    
    // ===== SEQUENTIAL READ PHASE =====
    printf("\nSEQUENTIAL READ PHASE\n");
    printf("--------------------\n");
    
    uint64_t* seq_latencies = malloc(NUM_DOCS * sizeof(uint64_t));
    
    engine_stats_t seq_start_stats = engine_stats(engine);
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < NUM_DOCS; i++) {
        struct timespec t1, t2;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        
        document_t* doc = engine_get(engine, keys[i]);
        // Prevent optimization by "using" the data
        if (doc) {
            volatile uint8_t v = doc->payload.data[0];
            (void)v;
        }
        
        clock_gettime(CLOCK_MONOTONIC, &t2);
        seq_latencies[i] = timespec_to_ns(&t2) - timespec_to_ns(&t1);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    uint64_t seq_read_ns = timespec_to_ns(&end) - timespec_to_ns(&start);
    engine_stats_t seq_end_stats = engine_stats(engine);
    
    printf("Total time: %.2f ms\n", seq_read_ns / 1e6);
    printf("Throughput: %.0f ops/sec\n", NUM_DOCS / (seq_read_ns / 1e9));
    
    size_t seq_hot_hits = seq_end_stats.hot_hits - seq_start_stats.hot_hits;
    size_t seq_cold_hits = seq_end_stats.cold_hits - seq_start_stats.cold_hits;
    printf("Cache: %zu hot hits (%.1f%%), %zu cold hits (%.1f%%)\n",
           seq_hot_hits,
           100.0 * seq_hot_hits / NUM_DOCS,
           seq_cold_hits,
           100.0 * seq_cold_hits / NUM_DOCS);
    
    print_latency_stats("Latencies", seq_latencies, NUM_DOCS);
    
    // ===== RANDOM READ PHASE (lock-free) =====
    printf("\n\nRANDOM READ PHASE\n");
    printf("-----------------\n");

    uint64_t* rand_latencies = malloc(NUM_DOCS * sizeof(uint64_t));

    // Create random permutation of indices
    int* indices = malloc(NUM_DOCS * sizeof(int));
    for (int i = 0; i < NUM_DOCS; i++) {
        indices[i] = i;
    }

    // Fisher-Yates shuffle with xorshift (no global rand() lock)
    uint32_t seed = 12345u;
    for (int i = NUM_DOCS - 1; i > 0; i--) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        int j = seed % (i + 1);
        int tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
    }

    // Local counters — no engine_stats() lock contention
    size_t local_hits = 0;
    size_t local_misses = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < NUM_DOCS; i++) {
        struct timespec t1, t2;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        
        document_t* doc = engine_get(engine, keys[indices[i]]);
        
        clock_gettime(CLOCK_MONOTONIC, &t2);
        rand_latencies[i] = timespec_to_ns(&t2) - timespec_to_ns(&t1);
        
        if (doc) {
            volatile uint8_t v = doc->payload.data[0];
            (void)v;
            local_hits++;
        } else {
            local_misses++;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    uint64_t rand_read_ns = timespec_to_ns(&end) - timespec_to_ns(&start);

    printf("Total time: %.2f ms\n", rand_read_ns / 1e6);
    printf("Throughput: %.0f ops/sec\n", NUM_DOCS / (rand_read_ns / 1e9));
    printf("Cache: %zu hits (%.1f%%), %zu misses (%.1f%%)\n",
        local_hits,
        100.0 * local_hits / NUM_DOCS,
        local_misses,
        100.0 * local_misses / NUM_DOCS);

    print_latency_stats("Latencies", rand_latencies, NUM_DOCS);
        
    // ===== FINAL STATS =====
    printf("\n\nFINAL STATE\n");
    printf("-----------\n");

    stats = engine_stats(engine);
    printf("Total gets: %zu\n", stats.total_gets);
    printf("Total sets: %zu\n", stats.total_sets);
    printf("Total dels: %zu\n", stats.total_dels);
    printf("Hot tier final: %zu docs, %.2f MB\n",
            hot_tier_doc_count(hot),
            hot_tier_used_bytes(hot) / 1e6);
    printf("Cold tier final: %zu docs, %.2f MB\n",
            cold_tier_doc_count(cold),
            cold_tier_total_bytes(cold) / 1e6);

    // Cleanup
    engine_destroy(engine);
    for (int i = 0; i < NUM_DOCS; i++) {
        free(keys[i]);
    }
    free(keys);
    free(seq_latencies);
    free(rand_latencies);
    free(indices);

    printf("\n========================================\n");
    return 0;
}