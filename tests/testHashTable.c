#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "calderadb/util/hashTable.h"

void test_hashtable_insert_and_lookup() {
    printf("test_hashtable_insert_and_lookup... ");
    
    hashtable_t* ht = hashtable_create(16);
    assert(ht != NULL);
    
    doc_id_t key = {.data = "test_1", .len = 6};
    document_t doc = {0};
    doc.id = key;
    
    assert(hashtable_insert(ht, &key, &doc));
    assert(hashtable_size(ht) == 1);
    
    document_t* result = hashtable_lookup(ht, &key);
    assert(result == &doc);
    
    hashtable_destroy(ht);
    printf("PASSED\n");
}

void test_hashtable_remove() {
    printf("test_hashtable_remove... ");
    
    hashtable_t* ht = hashtable_create(16);
    doc_id_t key = {.data = "test_1", .len = 6};
    document_t doc = {0};
    
    hashtable_insert(ht, &key, &doc);
    assert(hashtable_size(ht) == 1);
    
    assert(hashtable_remove(ht, &key));
    assert(hashtable_size(ht) == 0);
    
    hashtable_destroy(ht);
    printf("PASSED\n");
}

int main() {
    test_hashtable_insert_and_lookup();
    test_hashtable_remove();
    printf("All tests passed!\n");
    return 0;
}