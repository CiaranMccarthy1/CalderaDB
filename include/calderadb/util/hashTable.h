#ifndef CALDERADB_HASHTABLE_H
#define CALDERADB_HASHTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include "calderadb/core/types.h"

/* Opaque type */
typedef struct hashtable hashtable_t;

/* Entry */
typedef struct ht_entry {
    doc_id_t key;
    document_t* value;
    struct ht_entry* next;
} ht_entry_t;

/* Create/destroy */
hashtable_t* hashtable_create(size_t initial_capacity);
void hashtable_destroy(hashtable_t* ht);

/* Core operations */
bool hashtable_insert(hashtable_t* ht, const doc_id_t* key, document_t* value);
document_t* hashtable_lookup(const hashtable_t* ht, const doc_id_t* key);
bool hashtable_remove(hashtable_t* ht, const doc_id_t* key);

/* Iteration */
typedef struct ht_iter {
    const hashtable_t* ht;
    size_t bucket_index;
    ht_entry_t* current;
} ht_iter_t;

ht_iter_t* hashtable_iter_create(const hashtable_t* ht);
bool hashtable_iter_next(ht_iter_t* iter, doc_id_t* key, document_t** value);
void hashtable_iter_destroy(ht_iter_t* iter);

/* Statistics */
size_t hashtable_size(const hashtable_t* ht);
size_t hashtable_capacity(const hashtable_t* ht);
double hashtable_load_factor(const hashtable_t* ht);

#endif /* CALDERADB_HASHTABLE_H */