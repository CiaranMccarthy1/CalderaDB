#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include "calderadb/engine/engine.h"
#include "calderadb/core/types.h"

#define NUM_DOCS       650000
#define OPS_PER_THREAD 50000
#define HOT_CAPACITY   (128 * 1024 * 1024)
#define BENCH_DATA_DIR "/tmp/calderadb_bench_concurrency"

/* Latency sample buffer per thread for percentile calculation */
#define SAMPLE_CAP 50000

typedef struct {
    calderadb_engine_t* engine;
    char** keys;
    int num_keys;
    int thread_id;
    int is_writer;
    uint64_t ops_done;
    uint64_t ns_elapsed;
    uint64_t latencies[5]; /* [p50, p95, p99, p99.9, max] */
    uint64_t* samples;
} worker_args_t;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static int cmp_u64(const void* a, const void* b) {
    uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

static pthread_barrier_t g_barrier;

static void* worker(void* arg) {
    worker_args_t* w = (worker_args_t*)arg;
    srand(42 + w->thread_id);
    char new_key[32];

    pthread_barrier_wait(&g_barrier);
    uint64_t t0 = now_ns();

    for (int i = 0; i < OPS_PER_THREAD; i++) {
        uint64_t op_t0 = now_ns();
        if (w->is_writer) {
            snprintf(new_key, sizeof(new_key), "w%d_%d", w->thread_id, i);
            engine_set(w->engine, new_key, (const uint8_t*)"v", 1);
        } else {
            int idx = (w->thread_id + i * 997) % w->num_keys;
            engine_get(w->engine, w->keys[idx]);
        }
        w->samples[i] = now_ns() - op_t0;
    }

    w->ns_elapsed = now_ns() - t0;
    w->ops_done   = OPS_PER_THREAD;

    /* Compute percentiles */
    qsort(w->samples, OPS_PER_THREAD, sizeof(uint64_t), cmp_u64);
    w->latencies[0] = w->samples[(int)(OPS_PER_THREAD * 0.50)];
    w->latencies[1] = w->samples[(int)(OPS_PER_THREAD * 0.95)];
    w->latencies[2] = w->samples[(int)(OPS_PER_THREAD * 0.99)];
    w->latencies[3] = w->samples[(int)(OPS_PER_THREAD * 0.999)];
    w->latencies[4] = w->samples[OPS_PER_THREAD - 1];
    return NULL;
}

static void run_scenario(calderadb_engine_t* engine, char** keys,
                         int num_readers, int num_writers) {
    int total = num_readers + num_writers;
    printf("\n--- %d reader%s, %d writer%s ---\n",
           num_readers, num_readers == 1 ? "" : "s",
           num_writers, num_writers == 1 ? "" : "s");

    pthread_barrier_init(&g_barrier, NULL, total + 1);

    pthread_t* threads     = malloc(total * sizeof(pthread_t));
    worker_args_t* args    = calloc(total, sizeof(worker_args_t));

    for (int i = 0; i < total; i++) {
        args[i].engine    = engine;
        args[i].keys      = keys;
        args[i].num_keys  = NUM_DOCS;
        args[i].thread_id = i;
        args[i].is_writer = (i >= num_readers);
        args[i].samples   = malloc(OPS_PER_THREAD * sizeof(uint64_t));
        pthread_create(&threads[i], NULL, worker, &args[i]);
    }

    pthread_barrier_wait(&g_barrier);
    uint64_t wall_t0 = now_ns();
    for (int i = 0; i < total; i++) pthread_join(threads[i], NULL);
    double wall_ms = (now_ns() - wall_t0) / 1e6;

    uint64_t agg_ops = (uint64_t)total * OPS_PER_THREAD;
    double agg_thr   = agg_ops / (wall_ms / 1000.0);
    double pt_thr    = agg_thr / total;

    /* Aggregate percentiles: median across threads */
    double p50 = 0, p95 = 0, p99 = 0, p999 = 0, pmax = 0;
    for (int i = 0; i < total; i++) {
        p50  += args[i].latencies[0];
        p95  += args[i].latencies[1];
        p99  += args[i].latencies[2];
        p999 += args[i].latencies[3];
        if (args[i].latencies[4] > (uint64_t)pmax) pmax = args[i].latencies[4];
    }
    p50 /= total; p95 /= total; p99 /= total; p999 /= total;

    printf("Wall time:         %.2f ms\n",      wall_ms);
    printf("Aggregate ops/sec: %'.0f\n",         agg_thr);
    printf("Per-thread avg:    %'.0f ops/sec\n", pt_thr);
    printf("Per-thread p50:    %.2f us\n",       p50  / 1e3);
    printf("Per-thread p95:    %.2f us\n",       p95  / 1e3);
    printf("Per-thread p99:    %.2f us\n",       p99  / 1e3);
    printf("Per-thread p99.9:  %.2f us\n",       p999 / 1e3);
    printf("Per-thread max:    %.2f us\n",       pmax / 1e3);

    for (int i = 0; i < total; i++) free(args[i].samples);
    free(args);
    free(threads);
    pthread_barrier_destroy(&g_barrier);
}

int main(void) {
    printf("========== CalderaDB Concurrency Benchmark ==========\n");
    system("rm -rf " BENCH_DATA_DIR);
    calderadb_engine_t* engine = engine_create(HOT_CAPACITY, BENCH_DATA_DIR);
    if (!engine) { fprintf(stderr, "Failed to create engine\n"); return 1; }

    /* Pre-populate */
    char** keys = malloc(NUM_DOCS * sizeof(char*));
    printf("Pre-populating %d docs...\n", NUM_DOCS);
    uint64_t pop_t0 = now_ns();
    for (int i = 0; i < NUM_DOCS; i++) {
        keys[i] = malloc(32);
        snprintf(keys[i], 32, "key_%d", i);
        engine_set(engine, keys[i], (const uint8_t*)"val", 3);
    }
    printf("Done in %.2f ms\n", (now_ns() - pop_t0) / 1e6);

    run_scenario(engine, keys, 16, 0);
    run_scenario(engine, keys,  8, 1);
    run_scenario(engine, keys,  4, 4);
    run_scenario(engine, keys,  0, 8);

    printf("\n=================================================\n");

    engine_destroy(engine);
    for (int i = 0; i < NUM_DOCS; i++) free(keys[i]);
    free(keys);
    return 0;
}
