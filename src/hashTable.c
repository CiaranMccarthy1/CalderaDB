#include "calderadb/util/hashTable.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL

static uint64_t fnv_hash(const uint8_t* data, size_t len) {
    uint64_t hash = FNV_OFFSET;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

struct hashtable {
    ht_entry_t** buckets;
    size_t capacity;
    size_t size;
};

hashtable_t* hashtable_create(size_t initial_capacity) {
    hashtable_t* ht = calloc(1, sizeof(hashtable_t));
    if (!ht) return NULL;
    
    ht->capacity = initial_capacity ? initial_capacity : 16;
    ht->buckets = calloc(ht->capacity, sizeof(ht_entry_t*));
    if (!ht->buckets) {
        free(ht);
        return NULL;
    }
    ht->size = 0;
    return ht;
}

void hashtable_destroy(hashtable_t* ht) {
    if (!ht) return;
    
    for (size_t i = 0; i < ht->capacity; i++) {
        ht_entry_t* entry = ht->buckets[i];
        while (entry) {
            ht_entry_t* next = entry->next;
            doc_id_free(&entry->key);
            free(entry);
            entry = next;
        }
    }
    free(ht->buckets);
    free(ht);
}

static void hashtable_resize(hashtable_t* ht) {
    size_t new_capacity = ht->capacity * 2;
    ht_entry_t** new_buckets = calloc(new_capacity, sizeof(ht_entry_t*));
    if (!new_buckets) return;
    
    for (size_t i = 0; i < ht->capacity; i++) {
        ht_entry_t* entry = ht->buckets[i];
        while (entry) {
            ht_entry_t* next = entry->next;
            uint64_t hash = fnv_hash((const uint8_t*)entry->key.data, entry->key.len);
            size_t index = hash % new_capacity;
            entry->next = new_buckets[index];
            new_buckets[index] = entry;
            entry = next;
        }
    }
    
    free(ht->buckets);
    ht->buckets = new_buckets;
    ht->capacity = new_capacity;
}

bool hashtable_insert(hashtable_t* ht, const doc_id_t* key, document_t* value) {
    if (!ht || !key || !value) return false;
    
    if (ht->size >= ht->capacity * 0.75) {
        hashtable_resize(ht);
    }
    
    uint64_t hash = fnv_hash((const uint8_t*)key->data, key->len);
    size_t index = hash % ht->capacity;
    
    /* Check if key already exists */
    ht_entry_t* entry = ht->buckets[index];
    while (entry) {
        if (entry->key.len == key->len && 
            memcmp(entry->key.data, key->data, key->len) == 0) {
            entry->value = value;
            return true;
        }
        entry = entry->next;
    }
    
    ht_entry_t* new_entry = malloc(sizeof(ht_entry_t));
    if (!new_entry) return false;
    
    new_entry->key.data = malloc(key->len);
    if (!new_entry->key.data) {
        free(new_entry);
        return false;
    }
    memcpy(new_entry->key.data, key->data, key->len);
    new_entry->key.len = key->len;
    new_entry->value = value;
    new_entry->next = ht->buckets[index];
    ht->buckets[index] = new_entry;
    ht->size++;
    
    return true;
}

document_t* hashtable_lookup(const hashtable_t* ht, const doc_id_t* key) {
    if (!ht || !key) return NULL;
    
    uint64_t hash = fnv_hash((const uint8_t*)key->data, key->len);
    size_t index = hash % ht->capacity;
    
    ht_entry_t* entry = ht->buckets[index];
    while (entry) {
        if (entry->key.len == key->len && 
            memcmp(entry->key.data, key->data, key->len) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    
    return NULL;
}

bool hashtable_remove(hashtable_t* ht, const doc_id_t* key) {
    if (!ht || !key) return false;
    
    uint64_t hash = fnv_hash((const uint8_t*)key->data, key->len);
    size_t index = hash % ht->capacity;
    
    ht_entry_t* entry = ht->buckets[index];
    ht_entry_t* prev = NULL;
    
    while (entry) {
        if (entry->key.len == key->len && 
            memcmp(entry->key.data, key->data, key->len) == 0) {
            if (prev) {
                prev->next = entry->next;
            } else {
                ht->buckets[index] = entry->next;
            }
            doc_id_free(&entry->key);
            free(entry);
            ht->size--;
            return true;
        }
        prev = entry;
        entry = entry->next;
    }
    
    return false;
}

ht_iter_t* hashtable_iter_create(const hashtable_t* ht) {
    if (!ht) return NULL;
    
    ht_iter_t* iter = malloc(sizeof(ht_iter_t));
    if (!iter) return NULL;
    
    iter->ht = ht;
    iter->bucket_index = 0;
    iter->current = NULL;
    
    /* Find first non-empty bucket */
    while (iter->bucket_index < ht->capacity && !ht->buckets[iter->bucket_index]) {
        iter->bucket_index++;
    }
    if (iter->bucket_index < ht->capacity) {
        iter->current = ht->buckets[iter->bucket_index];
    }
    
    return iter;
}

bool hashtable_iter_next(ht_iter_t* iter, doc_id_t* key, document_t** value) {
    if (!iter || !iter->current) return false;
    
    if (key) {
        key->data = iter->current->key.data;
        key->len = iter->current->key.len;
    }
    if (value) {
        *value = iter->current->value;
    }
    
    iter->current = iter->current->next;
    
    if (!iter->current) {
        iter->bucket_index++;
        while (iter->bucket_index < iter->ht->capacity && !iter->ht->buckets[iter->bucket_index]) {
            iter->bucket_index++;
        }
        if (iter->bucket_index < iter->ht->capacity) {
            iter->current = iter->ht->buckets[iter->bucket_index];
        }
    }
    
    return true;
}

void hashtable_iter_destroy(ht_iter_t* iter) {
    free(iter);
}

size_t hashtable_size(const hashtable_t* ht) {
    return ht ? ht->size : 0;
}

size_t hashtable_capacity(const hashtable_t* ht) {
    return ht ? ht->capacity : 0;
}

double hashtable_load_factor(const hashtable_t* ht) {
    return ht ? (double)ht->size / ht->capacity : 0.0;
}