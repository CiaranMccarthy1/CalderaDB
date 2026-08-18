#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include "calderadb/hot/hotTier.h"
#include "calderadb/core/types.h"

void test_eviction_lru_order() {
    printf("test_eviction_lru_order... ");
    
    // capacity for slightly more than 3 small docs
    hot_tier_t* tier = hot_tier_create(400);
    assert(tier != NULL);
    
    document_t* doc0 = document_create("doc_0", (const uint8_t*)"000", 3);
    document_t* doc1 = document_create("doc_1", (const uint8_t*)"111", 3);
    document_t* doc2 = document_create("doc_2", (const uint8_t*)"222", 3);
    
    assert(hot_tier_insert(tier, doc0));
    doc0->last_accessed = 100;
    assert(hot_tier_insert(tier, doc1));
    doc1->last_accessed = 200;
    assert(hot_tier_insert(tier, doc2));
    doc2->last_accessed = 300;
    
    // Access doc_0 to make it recently used
    doc_id_t id0 = doc_id_from_string("doc_0");
    document_t* accessed = hot_tier_get(tier, &id0);
    assert(accessed != NULL);
    // Since hot_tier_get uses coarse time(), let's manually override to be the highest
    accessed->last_accessed = 400;
    
    // Evict one - should be doc_1 because doc_0 was recently accessed (400), and doc_1 (200) is older than doc_2 (300)
    document_t* evicted = hot_tier_evict_one(tier);
    assert(evicted != NULL);
    printf("EVICTED: %s\n", evicted->id.data);
    assert(strcmp(evicted->id.data, "doc_1") == 0);
    
    document_free(evicted);
    doc_id_free(&id0);
    hot_tier_destroy(tier);
    
    printf("PASSED\n");
}

void test_eviction_capacity_limit() {
    printf("test_eviction_capacity_limit... ");
    
    // Tiny capacity
    hot_tier_t* tier = hot_tier_create(150);
    assert(tier != NULL);
    
    document_t* doc1 = document_create("doc_cap1", (const uint8_t*)"data", 4);
    assert(hot_tier_insert(tier, doc1));
    
    document_t* doc2 = document_create("doc_cap2", (const uint8_t*)"data_very_large_to_force_eviction", 33);
    
    // Insertion might succeed by evicting doc1, or fail if doc2 is larger than tier capacity
    // Either way, doc_count shouldn't exceed capacity bounds.
    hot_tier_insert(tier, doc2);
    
    assert(hot_tier_doc_count(tier) <= 1);
    
    hot_tier_destroy(tier);
    
    printf("PASSED\n");
}

int main() {
    test_eviction_lru_order();
    test_eviction_capacity_limit();
    printf("All eviction tests passed!\n");
    return 0;
}
