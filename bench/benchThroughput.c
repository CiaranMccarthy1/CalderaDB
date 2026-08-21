#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "calderadb/engine/engine.h"
#include "calderadb/core/types.h"

#define NUM_DOCS 650000

uint64_t timespec_to_ns(struct timespec* ts) {
    return ts->tv_sec * (uint64_t)1e9 + ts->tv_nsec;
}

int main() {
    size_t hot_capacity = 128 * 1024 * 1024; 

    printf("========== CalderaDB Throughput Benchmark ==========\n");
    printf("Documents: %d, Payload: 100 bytes, Hot tier: %zu MB\n\n", NUM_DOCS, hot_capacity / (1024 * 1024));
    
    system("rm -rf /tmp/calderadb_bench_tp");
    
    calderadb_engine_t* engine = engine_create(hot_capacity, "/tmp/calderadb_bench_tp");
    assert(engine != NULL);
    
    char** keys = malloc(NUM_DOCS * sizeof(char*));
    for (int i = 0; i < NUM_DOCS; i++) {
        keys[i] = malloc(32);
        snprintf(keys[i], 32, "key_%d", i);
    }
    
    // 100 bytes payload to force hot/cold tier interaction
    char val[101];
    memset(val, 'A', 100);
    val[100] = '\0';
    size_t val_len = 100;  // Use actual length, not strlen
    
    // ===== INSERT PHASE =====
    printf("INSERT PHASE\n");
    printf("-----------\n");
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < NUM_DOCS; i++) {
        engine_set(engine, keys[i], (const uint8_t*)val, val_len);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    uint64_t insert_ns = timespec_to_ns(&end) - timespec_to_ns(&start);
    printf("Inserted %d docs in %.2f ms\n", NUM_DOCS, insert_ns / 1e6);
    printf("Insert throughput: %.0f ops/sec\n", NUM_DOCS / (insert_ns / 1e9));
    
    // Check tier state after inserts
    hot_tier_t* hot = engine_hot_tier(engine);
    cold_tier_t* cold = engine_cold_tier(engine);
    
    printf("Hot tier: %zu docs, %.2f KB / %.2f KB (%.1f%% full)\n",
           hot_tier_doc_count(hot),
           hot_tier_used_bytes(hot) / 1024.0,
           hot_tier_capacity_bytes(hot) / 1024.0,
           100.0 * hot_tier_used_bytes(hot) / hot_tier_capacity_bytes(hot));
    printf("Cold tier: %zu docs, %.2f KB\n",
           cold_tier_doc_count(cold),
           cold_tier_total_bytes(cold) / 1024.0);
    
    // ===== RANDOM READ PHASE =====
    printf("\nRANDOM READ PHASE\n");
    printf("-----------------\n");
    printf("Reading %d random docs...\n", NUM_DOCS);
    
    srand(42);  // Fixed seed for reproducibility
    
    // Capture stats BEFORE reads
    engine_stats_t stats_before = engine_stats(engine);
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < NUM_DOCS; i++) {
        int r = rand() % NUM_DOCS;
        document_t* doc = engine_get(engine, keys[r]);
        // Prevent optimization by "using" the data
        if (doc) {
            volatile uint8_t v = doc->payload.data[0];
            (void)v;
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Capture stats AFTER reads
    engine_stats_t stats_after = engine_stats(engine);
    
    uint64_t read_ns = timespec_to_ns(&end) - timespec_to_ns(&start);
    printf("Read %d docs in %.2f ms\n", NUM_DOCS, read_ns / 1e6);
    printf("Read throughput: %.0f ops/sec\n", NUM_DOCS / (read_ns / 1e9));
    
    // Calculate hit rates for THIS read phase only
    size_t hot_hits_phase = stats_after.hot_hits - stats_before.hot_hits;
    size_t cold_hits_phase = stats_after.cold_hits - stats_before.cold_hits;
    size_t total_hits_phase = hot_hits_phase + cold_hits_phase;
    
    double hot_hit_rate = (total_hits_phase > 0) ? 
        (double)hot_hits_phase / total_hits_phase * 100.0 : 0.0;
    double cold_hit_rate = (total_hits_phase > 0) ? 
        (double)cold_hits_phase / total_hits_phase * 100.0 : 0.0;
    
    printf("\nCache Performance (this phase):\n");
    printf("  Hot hits:  %zu (%.2f%%)\n", hot_hits_phase, hot_hit_rate);
    printf("  Cold hits: %zu (%.2f%%)\n", cold_hits_phase, cold_hit_rate);
    
    // ===== WORKING SET ANALYSIS =====
    printf("\nWorking Set Analysis\n");
    printf("-------------------\n");
    
    size_t avg_doc_size = val_len + 64;  // payload + overhead
    size_t docs_that_fit_hot = hot_capacity / avg_doc_size;
    
    printf("Average doc size: ~%zu bytes\n", avg_doc_size);
    printf("Docs that fit in hot tier: ~%zu\n", docs_that_fit_hot);
    printf("Actual docs in hot tier: %zu\n", hot_tier_doc_count(hot));
    printf("Docs in cold tier: %zu\n", cold_tier_doc_count(cold));
    
    // Estimate cold read cost
    if (cold_hits_phase > 0) {
        double avg_cold_latency_ms = (read_ns / 1e6) * (cold_hits_phase / (double)NUM_DOCS);
        printf("\nEstimated cost of cold reads: ~%.2f ms total\n", avg_cold_latency_ms);
        printf("(Cold reads are much slower than hot reads)\n");
    }
    
    // ===== FINAL STATE =====
    printf("\n\nFINAL STATE\n");
    printf("-----------\n");
    
    engine_stats_t final_stats = engine_stats(engine);
    printf("Total engine stats:\n");
    printf("  Total gets: %zu\n", final_stats.total_gets);
    printf("  Total sets: %zu\n", final_stats.total_sets);
    printf("  Total dels: %zu\n", final_stats.total_dels);
    printf("  Total hot hits: %zu\n", final_stats.hot_hits);
    printf("  Total cold hits: %zu\n", final_stats.cold_hits);
    printf("  Total misses: %zu\n", final_stats.misses);
    
    printf("\nTier capacities:\n");
    printf("  Hot tier: %.2f KB / %.2f KB used (%.1f%%)\n",
           hot_tier_used_bytes(hot) / 1024.0,
           hot_tier_capacity_bytes(hot) / 1024.0,
           100.0 * hot_tier_used_bytes(hot) / hot_tier_capacity_bytes(hot));
    printf("  Cold tier: %.2f KB total\n",
           cold_tier_total_bytes(cold) / 1024.0);
    
    // ===== RECOMMENDATIONS =====
    printf("\n\nPERFORMANCE ANALYSIS\n");
    printf("-------------------\n");
    
    if (hot_hit_rate > 90.0) {
        printf("✓ Excellent hot tier hit rate (>90%%)\n");
        printf("  Most reads are satisfied from memory\n");
    } else if (hot_hit_rate > 70.0) {
        printf("~ Good hot tier hit rate (70-90%%)\n");
        printf("  Eviction policy is reasonable\n");
    } else {
        printf("⚠ Poor hot tier hit rate (<70%%)\n");
        printf("  Consider: larger hot tier, better eviction policy, or workload-specific optimization\n");
    }
    
    if (cold_hits_phase > 0) {
        printf("\nCold reads detected: %zu (%.2f%% of workload)\n", 
               cold_hits_phase, cold_hit_rate);
        printf("Cold tier is providing overflow storage but at I/O cost\n");
    }
    
    // Cleanup
    engine_destroy(engine);
    for (int i = 0; i < NUM_DOCS; i++) {
        free(keys[i]);
    }
    free(keys);
    
    printf("\n=================================================\n");
    return 0;
}