#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "calderadb/hot/hotTier.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct hot_tier {
    hashtable_t* documents;
    size_t used_bytes;
    size_t capacity_bytes;
    size_t doc_count;
    pthread_rwlock_t lock;
};

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
    pthread_rwlock_init(&tier->lock, NULL);
    
    return tier;
}

void hot_tier_destroy(hot_tier_t* tier) {
    if (!tier) return;
    
    /* Note: We don't destroy documents here - caller manages them */
    hashtable_destroy(tier->documents);
    pthread_rwlock_destroy(&tier->lock);
    free(tier);
}

bool hot_tier_insert(hot_tier_t* tier, document_t* doc) {
    if (!tier || !doc) return false;
    
    pthread_rwlock_wrlock(&tier->lock);
    
    /* Check capacity */
    size_t doc_size = doc->size_bytes + sizeof(doc_id_t) + sizeof(document_t);
    if (tier->used_bytes + doc_size > tier->capacity_bytes) {
        /* Evict one document to make room */
        document_t* evicted = hot_tier_evict_one(tier);
        if (evicted) {
            /* Document is evicted - caller should move to cold tier */
            doc->location = TIER_COLD;
        } else {
            pthread_rwlock_unlock(&tier->lock);
            return false;
        }
    }
    
    /* Insert into hash table */
    if (!hashtable_insert(tier->documents, &doc->id, doc)) {
        pthread_rwlock_unlock(&tier->lock);
        return false;
    }
    
    tier->used_bytes += doc_size;
    tier->doc_count++;
    doc->location = TIER_HOT;
    
    pthread_rwlock_unlock(&tier->lock);
    return true;
}

document_t* hot_tier_get(hot_tier_t* tier, const doc_id_t* id) {
    if (!tier || !id) return NULL;
    
    pthread_rwlock_rdlock(&tier->lock);
    document_t* doc = hashtable_lookup(tier->documents, id);
    
    if (doc) {
        doc->access_count++;
        doc->last_accessed = (timestamp_t)time(NULL) * 1000; /* milliseconds */
    }
    
    pthread_rwlock_unlock(&tier->lock);
    return doc;
}

bool hot_tier_remove(hot_tier_t* tier, const doc_id_t* id) {
    if (!tier || !id) return false;
    
    pthread_rwlock_wrlock(&tier->lock);
    
    document_t* doc = hashtable_lookup(tier->documents, id);
    if (!doc) {
        pthread_rwlock_unlock(&tier->lock);
        return false;
    }
    
    size_t doc_size = doc->size_bytes + sizeof(doc_id_t) + sizeof(document_t);
    if (hashtable_remove(tier->documents, id)) {
        tier->used_bytes -= doc_size;
        tier->doc_count--;
        pthread_rwlock_unlock(&tier->lock);
        return true;
    }
    
    pthread_rwlock_unlock(&tier->lock);
    return false;
}

document_t* hot_tier_evict_one(hot_tier_t* tier) {
    if (!tier) return NULL;
    
    pthread_rwlock_wrlock(&tier->lock);
    
    /* Simple LRU: find document with oldest last_accessed */
    document_t* victim = NULL;
    timestamp_t oldest = (timestamp_t)-1;
    
    ht_iter_t* iter = hashtable_iter_create(tier->documents);
    doc_id_t key;
    document_t* doc;
    
    while (hashtable_iter_next(iter, &key, &doc)) {
        if (doc && doc->last_accessed < oldest) {
            oldest = doc->last_accessed;
            victim = doc;
        }
    }
    hashtable_iter_destroy(iter);
    
    if (victim) {
        if (hashtable_remove(tier->documents, &victim->id)) {
            size_t doc_size = victim->size_bytes + sizeof(doc_id_t) + sizeof(document_t);
            tier->used_bytes -= doc_size;
            tier->doc_count--;
            victim->location = TIER_COLD;
            pthread_rwlock_unlock(&tier->lock);
            return victim;
        }
    }
    
    pthread_rwlock_unlock(&tier->lock);
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