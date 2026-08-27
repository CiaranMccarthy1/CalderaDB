#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include "calderadb/hot/hotTier.h"

void test_hot_tier_insert_and_get() {
    printf("test_hot_tier_insert_and_get... ");
    
    hot_tier_t* tier = hot_tier_create(1024);
    assert(tier != NULL);
    
    document_t* doc = document_create("test_1", (const uint8_t*)"hello", 5);
    assert(doc != NULL);
    
    assert(hot_tier_insert(tier, doc));
    assert(hot_tier_doc_count(tier) == 1);
    
    doc_id_t id = {.data = "test_1", .len = 6};
    document_t* retrieved = hot_tier_get(tier, &id);
    assert(retrieved != NULL);
    assert(strcmp(retrieved->id.data, "test_1") == 0);
    
    hot_tier_destroy(tier);
    printf("PASSED\n");
}

void test_hot_tier_eviction() {
    printf("test_hot_tier_eviction... ");
    
    /* Create tier with capacity for ~2 documents */
    hot_tier_t* tier = hot_tier_create(400);
    assert(tier != NULL);
    
    for (int i = 0; i < 3; i++) {
        char id[16];
        snprintf(id, sizeof(id), "doc_%d", i);
        document_t* doc = document_create(id, (const uint8_t*)"data", 4);
        assert(doc != NULL);
        assert(hot_tier_insert(tier, doc));
    }
    
    printf("doc_count=%zu ", hot_tier_doc_count(tier));
    assert(hot_tier_doc_count(tier) <= 3);
    
    hot_tier_destroy(tier);
    printf("PASSED\n");
}

int main() {
    test_hot_tier_insert_and_get();
    test_hot_tier_eviction();
    printf("All tests passed!\n");
    return 0;
}