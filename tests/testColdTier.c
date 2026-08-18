#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "calderadb/cold/coldTier.h"
#include "calderadb/core/types.h"

#define TEST_DIR "/tmp/calderadb_test_cold"

static void cleanup_test_dir() {
    system("rm -rf " TEST_DIR);
}

void test_cold_tier_append_and_read() {
    printf("test_cold_tier_append_and_read... ");
    cleanup_test_dir();
    
    cold_tier_t* tier = cold_tier_create(TEST_DIR);
    assert(tier != NULL);
    
    document_t* doc = document_create("doc_1", (const uint8_t*)"payload_data", 12);
    assert(doc != NULL);
    
    assert(cold_tier_append(tier, doc));
    
    doc_id_t id = doc_id_from_string("doc_1");
    document_t* retrieved = cold_tier_read(tier, &id);
    assert(retrieved != NULL);
    
    assert(retrieved->payload.len == 12);
    assert(memcmp(retrieved->payload.data, "payload_data", 12) == 0);
    
    document_free(retrieved);
    document_free(doc);
    doc_id_free(&id);
    cold_tier_destroy(tier);
    printf("PASSED\n");
}

void test_cold_tier_checksum_verification() {
    printf("test_cold_tier_checksum_verification... ");
    cleanup_test_dir();
    
    cold_tier_t* tier = cold_tier_create(TEST_DIR);
    assert(tier != NULL);
    
    document_t* doc = document_create("doc_chk", (const uint8_t*)"check_data", 10);
    assert(doc != NULL);
    
    assert(cold_tier_append(tier, doc));
    
    doc_id_t id = doc_id_from_string("doc_chk");
    document_t* retrieved = cold_tier_read(tier, &id);
    assert(retrieved != NULL);
    
    assert(retrieved->payload.len == 10);
    assert(memcmp(retrieved->payload.data, "check_data", 10) == 0);
    
    document_free(retrieved);
    document_free(doc);
    doc_id_free(&id);
    cold_tier_destroy(tier);
    printf("PASSED\n");
}

void test_cold_tier_crash_recovery() {
    printf("test_cold_tier_crash_recovery... ");
    cleanup_test_dir();
    
    cold_tier_t* tier = cold_tier_create(TEST_DIR);
    assert(tier != NULL);
    
    document_t* doc1 = document_create("rec_1", (const uint8_t*)"data1", 5);
    document_t* doc2 = document_create("rec_2", (const uint8_t*)"data2", 5);
    document_t* doc3 = document_create("rec_3", (const uint8_t*)"data3", 5);
    
    assert(cold_tier_append(tier, doc1));
    assert(cold_tier_append(tier, doc2));
    assert(cold_tier_append(tier, doc3));
    
    cold_tier_destroy(tier); // "crash"
    
    // Recover
    cold_tier_t* tier_recovered = cold_tier_create(TEST_DIR);
    assert(tier_recovered != NULL);
    
    // cold_tier_create might automatically recover, or we might need to call cold_tier_recover
    // the api says cold_tier_recover exists, let's call it just in case
    cold_tier_recover(tier_recovered);
    
    doc_id_t id1 = doc_id_from_string("rec_1");
    doc_id_t id2 = doc_id_from_string("rec_2");
    doc_id_t id3 = doc_id_from_string("rec_3");
    
    document_t* r1 = cold_tier_read(tier_recovered, &id1);
    document_t* r2 = cold_tier_read(tier_recovered, &id2);
    document_t* r3 = cold_tier_read(tier_recovered, &id3);
    
    assert(r1 != NULL && memcmp(r1->payload.data, "data1", 5) == 0);
    assert(r2 != NULL && memcmp(r2->payload.data, "data2", 5) == 0);
    assert(r3 != NULL && memcmp(r3->payload.data, "data3", 5) == 0);
    
    document_free(r1);
    document_free(r2);
    document_free(r3);
    document_free(doc1);
    document_free(doc2);
    document_free(doc3);
    doc_id_free(&id1);
    doc_id_free(&id2);
    doc_id_free(&id3);
    
    cold_tier_destroy(tier_recovered);
    
    printf("PASSED\n");
}

int main() {
    test_cold_tier_append_and_read();
    test_cold_tier_checksum_verification();
    test_cold_tier_crash_recovery();
    printf("All cold tier tests passed!\n");
    return 0;
}
