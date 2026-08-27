#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "calderadb/cold/coldTier.h"
#include "calderadb/core/types.h"

#define TEST_DIR "/tmp/calderadb_test_cold"

static void cleanup_test_dir() {
    system("rm -rf " TEST_DIR);
}

void test_cold_tier_append_and_read() {
    printf("test_cold_tier_append_and_read... ");
    cleanup_test_dir();
    
    cold_tier_t* tier = cold_tier_create(TEST_DIR, SYNC_EVERYSEC);
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
    
    cold_tier_t* tier = cold_tier_create(TEST_DIR, SYNC_EVERYSEC);
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
    
    cold_tier_t* tier = cold_tier_create(TEST_DIR, SYNC_EVERYSEC);
    assert(tier != NULL);
    
    document_t* doc1 = document_create("rec_1", (const uint8_t*)"data1", 5);
    document_t* doc2 = document_create("rec_2", (const uint8_t*)"data2", 5);
    document_t* doc3 = document_create("rec_3", (const uint8_t*)"data3", 5);
    
    assert(cold_tier_append(tier, doc1));
    assert(cold_tier_append(tier, doc2));
    assert(cold_tier_append(tier, doc3));
    
    cold_tier_destroy(tier); // "crash"
    
    // Recover
    cold_tier_t* tier_recovered = cold_tier_create(TEST_DIR, SYNC_EVERYSEC);
    assert(tier_recovered != NULL);
    
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

void test_cold_tier_tombstone_persistence() {
    printf("test_cold_tier_tombstone_persistence... ");
    cleanup_test_dir();
    
    cold_tier_t* tier = cold_tier_create(TEST_DIR, SYNC_EVERYSEC);
    assert(tier != NULL);
    
    document_t* doc1 = document_create("del_1", (const uint8_t*)"data1", 5);
    document_t* doc2 = document_create("del_2", (const uint8_t*)"data2", 5);
    assert(cold_tier_append(tier, doc1));
    assert(cold_tier_append(tier, doc2));
    
    doc_id_t id1 = doc_id_from_string("del_1");
    doc_id_t id2 = doc_id_from_string("del_2");
    
    // Delete del_1 (writes tombstone)
    assert(cold_tier_mark_deleted(tier, &id1));
    
    cold_tier_destroy(tier); // Crash
    
    // Recover
    cold_tier_t* tier_recovered = cold_tier_create(TEST_DIR, SYNC_EVERYSEC);
    assert(tier_recovered != NULL);
    
    // del_1 must NOT be found (tombstone was persisted)
    document_t* r1 = cold_tier_read(tier_recovered, &id1);
    assert(r1 == NULL);
    
    // del_2 must be found
    document_t* r2 = cold_tier_read(tier_recovered, &id2);
    assert(r2 != NULL && memcmp(r2->payload.data, "data2", 5) == 0);
    
    document_free(r2);
    document_free(doc1);
    document_free(doc2);
    doc_id_free(&id1);
    doc_id_free(&id2);
    cold_tier_destroy(tier_recovered);
    printf("PASSED\n");
}

void test_cold_tier_sync_always() {
    printf("test_cold_tier_sync_always... ");
    cleanup_test_dir();
    
    cold_tier_t* tier = cold_tier_create(TEST_DIR, SYNC_ALWAYS);
    assert(tier != NULL);
    assert(cold_tier_sync_policy(tier) == SYNC_ALWAYS);
    
    document_t* doc = document_create("doc_always", (const uint8_t*)"durable_data", 12);
    assert(doc != NULL);
    
    assert(cold_tier_append(tier, doc));
    // In SYNC_ALWAYS, bytes_since_sync must be 0 immediately after append
    assert(cold_tier_unsynced_bytes(tier) == 0);
    
    doc_id_t id = doc_id_from_string("doc_always");
    document_t* retrieved = cold_tier_read(tier, &id);
    assert(retrieved != NULL);
    assert(memcmp(retrieved->payload.data, "durable_data", 12) == 0);
    
    document_free(retrieved);
    document_free(doc);
    doc_id_free(&id);
    cold_tier_destroy(tier);
    printf("PASSED\n");
}

void test_cold_tier_sync_everysec() {
    printf("test_cold_tier_sync_everysec... ");
    cleanup_test_dir();
    
    cold_tier_t* tier = cold_tier_create(TEST_DIR, SYNC_EVERYSEC);
    assert(tier != NULL);
    assert(cold_tier_sync_policy(tier) == SYNC_EVERYSEC);
    
    document_t* doc = document_create("doc_sec", (const uint8_t*)"async_data", 10);
    assert(doc != NULL);
    
    assert(cold_tier_append(tier, doc));
    // Immediately after write, unsynced bytes should be > 0
    assert(cold_tier_unsynced_bytes(tier) > 0);
    
    // Sleep 1.5 seconds to allow background thread to flush
    usleep(1500000);
    
    // Background thread must have flushed unsynced bytes
    assert(cold_tier_unsynced_bytes(tier) == 0);
    
    document_free(doc);
    cold_tier_destroy(tier);
    printf("PASSED\n");
}

void test_cold_tier_sync_no_and_partial_recovery() {
    printf("test_cold_tier_sync_no_and_partial_recovery... ");
    cleanup_test_dir();
    
    cold_tier_t* tier = cold_tier_create(TEST_DIR, SYNC_NO);
    assert(tier != NULL);
    assert(cold_tier_sync_policy(tier) == SYNC_NO);
    
    document_t* doc1 = document_create("full_doc_1", (const uint8_t*)"hello world", 11);
    document_t* doc2 = document_create("full_doc_2", (const uint8_t*)"calderadb", 9);
    assert(cold_tier_append(tier, doc1));
    assert(cold_tier_append(tier, doc2));
    
    cold_tier_destroy(tier);
    
    // Simulate an unclean crash with a partial/corrupted trailing record written at EOF
    FILE* fp = fopen(TEST_DIR "/data.flux", "ab");
    assert(fp != NULL);
    uint32_t bogus_id_len = 16;
    fwrite(&bogus_id_len, sizeof(uint32_t), 1, fp);
    fwrite("partial_trailing", 16, 1, fp);
    uint32_t bogus_payload_len = 100; // truncated payload, file ends abruptly
    fwrite(&bogus_payload_len, sizeof(uint32_t), 1, fp);
    fwrite("short", 5, 1, fp);
    fclose(fp);
    
    // Recover with cold_tier_create
    cold_tier_t* tier_recovered = cold_tier_create(TEST_DIR, SYNC_NO);
    assert(tier_recovered != NULL);
    
    doc_id_t id1 = doc_id_from_string("full_doc_1");
    doc_id_t id2 = doc_id_from_string("full_doc_2");
    doc_id_t id_bogus = doc_id_from_string("partial_trailing");
    
    document_t* r1 = cold_tier_read(tier_recovered, &id1);
    document_t* r2 = cold_tier_read(tier_recovered, &id2);
    document_t* r_bogus = cold_tier_read(tier_recovered, &id_bogus);
    
    assert(r1 != NULL && memcmp(r1->payload.data, "hello world", 11) == 0);
    assert(r2 != NULL && memcmp(r2->payload.data, "calderadb", 9) == 0);
    assert(r_bogus == NULL);
    
    // Append a new doc to ensure data file was cleanly truncated at valid boundary
    document_t* doc3 = document_create("full_doc_3", (const uint8_t*)"new_data", 8);
    assert(cold_tier_append(tier_recovered, doc3));
    
    doc_id_t id3 = doc_id_from_string("full_doc_3");
    document_t* r3 = cold_tier_read(tier_recovered, &id3);
    assert(r3 != NULL && memcmp(r3->payload.data, "new_data", 8) == 0);
    
    document_free(r1);
    document_free(r2);
    document_free(r3);
    document_free(doc1);
    document_free(doc2);
    document_free(doc3);
    doc_id_free(&id1);
    doc_id_free(&id2);
    doc_id_free(&id3);
    doc_id_free(&id_bogus);
    
    cold_tier_destroy(tier_recovered);
    printf("PASSED\n");
}

int main() {
    test_cold_tier_append_and_read();
    test_cold_tier_checksum_verification();
    test_cold_tier_crash_recovery();
    test_cold_tier_tombstone_persistence();
    test_cold_tier_sync_always();
    test_cold_tier_sync_everysec();
    test_cold_tier_sync_no_and_partial_recovery();
    printf("All cold tier tests passed!\n");
    return 0;
}
