#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "calderadb/hot/hotTier.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

struct hot_tier {
    hashtable_t* documents;
    size_t used_bytes;
    size_t capacity_bytes;
    size_t doc_count;
    document_t* lru_head; // MRU
    document_t* lru_tail; // LRU (victim)
    pthread_rwlock_t ht_lock;
    pthread_mutex_t lru_lock;
};

// O(1) intrusive LRU list operations
static void lru_detach(hot_tier_t* tier, document_t* doc) {
    if (!tier || !doc) return;
    if (doc->prev) doc->prev->next = doc->next;
    else if (tier->lru_head == doc) tier->lru_head = doc->next;

    if (doc->next) doc->next->prev = doc->prev;
    else if (tier->lru_tail == doc) tier->lru_tail = doc->prev;

    doc->prev = NULL;
    doc->next = NULL;
}

static void lru_push_head(hot_tier_t* tier, document_t* doc) {
    if (!tier || !doc) return;
    doc->prev = NULL;
    doc->next = tier->lru_head;
    if (tier->lru_head) tier->lru_head->prev = doc;
    tier->lru_head = doc;
    if (!tier->lru_tail) tier->lru_tail = doc;
}

static void lru_touch(hot_tier_t* tier, document_t* doc) {
    if (tier->lru_head == doc) return;
    lru_detach(tier, doc);
    lru_push_head(tier, doc);
}

hot_tier_t* hot_tier_create(size_t capacity_bytes) {
    hot_tier_t* tier = calloc(1, sizeof(hot_tier_t));
    if (!tier) return NULL;
    
    tier->documents = hashtable_create(1024);
    if (!tier->documents) {
        free(tier);
        return NULL;
    }
    
    tier->capacity_bytes = capacity_bytes ? capacity_bytes : 1024 * 1024 * 1024; /* 1GB default */
    tier->used_bytes = 0;
    tier->doc_count = 0;
    pthread_rwlock_init(&tier->ht_lock, NULL);
    pthread_mutex_init(&tier->lru_lock, NULL);
    
    return tier;
}

void hot_tier_destroy(hot_tier_t* tier) {
    if (!tier) return;
    
    document_t* curr = tier->lru_head;
    while (curr) {
        document_t* next = curr->next;
        document_free(curr);
        curr = next;
    }
    hashtable_destroy(tier->documents);
    pthread_mutex_destroy(&tier->lru_lock);
    pthread_rwlock_destroy(&tier->ht_lock);
    free(tier);
}

bool hot_tier_insert(hot_tier_t* tier, document_t* doc) {
    if (!tier || !doc) return false;
    
    pthread_rwlock_wrlock(&tier->ht_lock);
    pthread_mutex_lock(&tier->lru_lock);
    
    document_t* existing = hashtable_lookup(tier->documents, &doc->id);
    if (existing) {
        lru_detach(tier, existing);
        hashtable_remove(tier->documents, &doc->id);
        size_t old_size = existing->size_bytes + sizeof(doc_id_t) + sizeof(document_t);
        tier->used_bytes -= old_size;
        tier->doc_count--;
        document_free(existing);
    }
    pthread_mutex_unlock(&tier->lru_lock);

    size_t doc_size = doc->size_bytes + sizeof(doc_id_t) + sizeof(document_t);
    if (tier->used_bytes + doc_size > tier->capacity_bytes) {
        pthread_rwlock_unlock(&tier->ht_lock);
        return false;
    }
    
    /* Insert into hash table */
    if (!hashtable_insert(tier->documents, &doc->id, doc)) {
        pthread_rwlock_unlock(&tier->ht_lock);
        return false;
    }
    
    pthread_mutex_lock(&tier->lru_lock);
    lru_push_head(tier, doc);
    pthread_mutex_unlock(&tier->lru_lock);
    tier->used_bytes += doc_size;
    tier->doc_count++;
    doc->location = TIER_HOT;
    
    pthread_rwlock_unlock(&tier->ht_lock);
    return true;
}

document_t* hot_tier_get(hot_tier_t* tier, const doc_id_t* id) {
    if (!tier || !id) return NULL;
    
    pthread_rwlock_rdlock(&tier->ht_lock);
    document_t* doc = hashtable_lookup(tier->documents, id);
    
    if (doc) {
        pthread_mutex_lock(&tier->lru_lock);
        doc->access_count++;
        doc->last_accessed = (timestamp_t)time(NULL) * 1000; /* milliseconds */
        lru_touch(tier, doc);
        pthread_mutex_unlock(&tier->lru_lock);
    }
    
    pthread_rwlock_unlock(&tier->ht_lock);
    return doc;
}

bool hot_tier_remove(hot_tier_t* tier, const doc_id_t* id) {
    if (!tier || !id) return false;
    
    pthread_rwlock_wrlock(&tier->ht_lock);
    
    document_t* doc = hashtable_lookup(tier->documents, id);
    if (!doc) {
        pthread_rwlock_unlock(&tier->ht_lock);
        return false;
    }
    
    pthread_mutex_lock(&tier->lru_lock);
    lru_detach(tier, doc);
    pthread_mutex_unlock(&tier->lru_lock);
    size_t doc_size = doc->size_bytes + sizeof(doc_id_t) + sizeof(document_t);
    if (hashtable_remove(tier->documents, id)) {
        tier->used_bytes -= doc_size;
        tier->doc_count--;
        pthread_rwlock_unlock(&tier->ht_lock);
        return true;
    }
    
    pthread_rwlock_unlock(&tier->ht_lock);
    return false;
}

document_t* hot_tier_evict_one(hot_tier_t* tier) {
    if (!tier) return NULL;
    
    pthread_rwlock_wrlock(&tier->ht_lock);
    
    pthread_mutex_lock(&tier->lru_lock);
    document_t* victim = tier->lru_tail;
    if (victim) {
        lru_detach(tier, victim);
        pthread_mutex_unlock(&tier->lru_lock);
        hashtable_remove(tier->documents, &victim->id);
        size_t doc_size = victim->size_bytes + sizeof(doc_id_t) + sizeof(document_t);
        tier->used_bytes -= doc_size;
        tier->doc_count--;
        victim->location = TIER_COLD;
        pthread_rwlock_unlock(&tier->ht_lock);
        return victim;
    }
    pthread_mutex_unlock(&tier->lru_lock);
    
    pthread_rwlock_unlock(&tier->ht_lock);
    return NULL;
}

size_t hot_tier_used_bytes(const hot_tier_t* tier) {
    return tier ? tier->used_bytes : 0;
}

size_t hot_tier_doc_count(const hot_tier_t* tier) {
    return tier ? tier->doc_count : 0;
}

size_t hot_tier_capacity_bytes(const hot_tier_t* tier) {
    return tier ? tier->capacity_bytes : 0;
}

hot_tier_iter_t* hot_tier_iter_create(hot_tier_t* tier) {
    if (!tier) return NULL;
    
    hot_tier_iter_t* iter = malloc(sizeof(hot_tier_iter_t));
    if (!iter) return NULL;
    
    iter->iter = hashtable_iter_create(tier->documents);
    return iter;
}

document_t* hot_tier_iter_next(hot_tier_iter_t* iter) {
    if (!iter || !iter->iter) return NULL;
    
    doc_id_t key;
    document_t* doc;
    if (hashtable_iter_next(iter->iter, &key, &doc)) {
        return doc;
    }
    return NULL;
}

void hot_tier_iter_destroy(hot_tier_iter_t* iter) {
    if (!iter) return;
    hashtable_iter_destroy(iter->iter);
    free(iter);
}