#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "calderadb/engine/engine.h"

#define TEST_DIR "/tmp/calderadb_test_engine"

static void cleanup_test_dir() {
    system("rm -rf " TEST_DIR);
}

void test_engine_basic_crud() {
    printf("test_engine_basic_crud... ");
    cleanup_test_dir();
    
    calderadb_engine_t* engine = engine_create(1024 * 1024, TEST_DIR);
    assert(engine != NULL);
    
    // SET
    assert(engine_set(engine, "k1", (const uint8_t*)"v1", 2));
    
    // GET
    document_t* doc = engine_get(engine, "k1");
    assert(doc != NULL);
    assert(doc->payload.len == 2);
    assert(memcmp(doc->payload.data, "v1", 2) == 0);
    
    // STATS
    engine_stats_t stats = engine_stats(engine);
    assert(stats.total_sets == 1);
    assert(stats.total_gets == 1);
    assert(stats.hot_hits == 1);
    
    // DEL
    assert(engine_del(engine, "k1"));
    assert(engine_get(engine, "k1") == NULL);
    
    engine_destroy(engine);
    printf("PASSED\n");
}

void test_engine_cold_fallback() {
    printf("test_engine_cold_fallback... ");
    cleanup_test_dir();
    
    // Tiny hot capacity: 100 bytes (cannot fit a 500-byte document)
    calderadb_engine_t* engine = engine_create(100, TEST_DIR);
    assert(engine != NULL);
    
    uint8_t large_payload[500];
    memset(large_payload, 'A', sizeof(large_payload));
    
    // engine_set should fallback to cold tier
    assert(engine_set(engine, "large_key", large_payload, sizeof(large_payload)));
    
    // engine_get should retrieve it from cold tier
    document_t* retrieved = engine_get(engine, "large_key");
    assert(retrieved != NULL);
    assert(retrieved->payload.len == sizeof(large_payload));
    assert(memcmp(retrieved->payload.data, large_payload, sizeof(large_payload)) == 0);
    document_free(retrieved);
    
    engine_destroy(engine);
    printf("PASSED\n");
}

#define NUM_READERS 4
#define READ_COUNT 1000

typedef struct {
    calderadb_engine_t* engine;
    const char* key;
} reader_arg_t;

static void* reader_thread(void* arg) {
    reader_arg_t* rarg = (reader_arg_t*)arg;
    for (int i = 0; i < READ_COUNT; i++) {
        document_t* doc = engine_get(rarg->engine, rarg->key);
        assert(doc != NULL);
        assert(doc->payload.len == 5);
    }
    return NULL;
}

void test_engine_concurrent_reads() {
    printf("test_engine_concurrent_reads... ");
    cleanup_test_dir();
    
    calderadb_engine_t* engine = engine_create(1024 * 1024, TEST_DIR);
    assert(engine != NULL);
    assert(engine_set(engine, "shared_key", (const uint8_t*)"hello", 5));
    
    pthread_t threads[NUM_READERS];
    reader_arg_t args[NUM_READERS];
    for (int i = 0; i < NUM_READERS; i++) {
        args[i].engine = engine;
        args[i].key = "shared_key";
        pthread_create(&threads[i], NULL, reader_thread, &args[i]);
    }
    
    for (int i = 0; i < NUM_READERS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    engine_stats_t stats = engine_stats(engine);
    assert(stats.total_gets == NUM_READERS * READ_COUNT);
    assert(stats.hot_hits == NUM_READERS * READ_COUNT);
    
    engine_destroy(engine);
    printf("PASSED\n");
}

int main() {
    test_engine_basic_crud();
    test_engine_cold_fallback();
    test_engine_concurrent_reads();
    printf("All engine tests passed!\n");
    return 0;
}
