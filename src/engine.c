#include "calderadb/engine/engine.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct calderadb_engine {
    hot_tier_t* hot;
    cold_tier_t* cold;
    engine_stats_t stats;
    pthread_rwlock_t lock; 
};

calderadb_engine_t* engine_create_with_sync_policy(size_t hot_capacity, const char* data_dir, sync_policy_t sync_policy) {
    calderadb_engine_t* engine = calloc(1, sizeof(calderadb_engine_t));
    if (!engine) return NULL;
    
    engine->hot = hot_tier_create(hot_capacity);
    if (!engine->hot) {
        free(engine);
        return NULL;
    }
    
    engine->cold = cold_tier_create(data_dir, sync_policy);
    if (!engine->cold) {
        hot_tier_destroy(engine->hot);
        free(engine);
        return NULL;
    }
    
    pthread_rwlock_init(&engine->lock, NULL);
    return engine;
}

calderadb_engine_t* engine_create(size_t hot_capacity, const char* data_dir) {
    return engine_create_with_sync_policy(hot_capacity, data_dir, SYNC_EVERYSEC);
}

void engine_destroy(calderadb_engine_t* engine) {
    if (!engine) return;
    hot_tier_destroy(engine->hot);
    cold_tier_destroy(engine->cold);
    pthread_rwlock_destroy(&engine->lock);
    free(engine);
}

document_t* engine_get(calderadb_engine_t* engine, const char* key) {
    if (!engine || !key) return NULL;
    
    doc_id_t doc_id = doc_id_from_string(key);
    
    // Fast path: try hot tier under read lock
    pthread_rwlock_rdlock(&engine->lock);
    __atomic_fetch_add(&engine->stats.total_gets, 1, __ATOMIC_RELAXED);
    
    document_t* doc = hot_tier_get(engine->hot, &doc_id);
    if (doc) {
        __atomic_fetch_add(&engine->stats.hot_hits, 1, __ATOMIC_RELAXED);
        pthread_rwlock_unlock(&engine->lock);
        doc_id_free(&doc_id);
        return doc;
    }
    pthread_rwlock_unlock(&engine->lock);
    
    // Slow path: acquire write lock to access cold tier and promote
    pthread_rwlock_wrlock(&engine->lock);
    
    // Double check hot tier
    doc = hot_tier_get(engine->hot, &doc_id);
    if (doc) {
        __atomic_fetch_add(&engine->stats.hot_hits, 1, __ATOMIC_RELAXED);
        pthread_rwlock_unlock(&engine->lock);
        doc_id_free(&doc_id);
        return doc;
    }
    
    doc = cold_tier_read(engine->cold, &doc_id);
    if (doc) {
        __atomic_fetch_add(&engine->stats.cold_hits, 1, __ATOMIC_RELAXED);
        
        // Promote to hot
        size_t doc_size = doc->size_bytes + sizeof(doc_id_t) + sizeof(document_t);
        while (hot_tier_used_bytes(engine->hot) + doc_size > hot_tier_capacity_bytes(engine->hot)) {
            document_t* evicted = hot_tier_evict_one(engine->hot);
            if (!evicted) break;
            cold_tier_append(engine->cold, evicted);
            document_free(evicted);
        }
        hot_tier_insert(engine->hot, doc);
    } else {
        __atomic_fetch_add(&engine->stats.misses, 1, __ATOMIC_RELAXED);
    }
    
    pthread_rwlock_unlock(&engine->lock);
    doc_id_free(&doc_id);
    return doc;
}

bool engine_set(calderadb_engine_t* engine, const char* key, const uint8_t* value, size_t value_len) {
    if (!engine || !key || !value) return false;
    
    document_t* doc = document_create(key, value, value_len);
    if (!doc) return false;
    
    pthread_rwlock_wrlock(&engine->lock);
    __atomic_fetch_add(&engine->stats.total_sets, 1, __ATOMIC_RELAXED);
    
    // Invalidate stale entry in cold tier if key is being updated
    doc_id_t doc_id = doc_id_from_string(key);
    cold_tier_mark_deleted(engine->cold, &doc_id);
    doc_id_free(&doc_id);
    
    size_t doc_size = doc->size_bytes + sizeof(doc_id_t) + sizeof(document_t);
    while (hot_tier_used_bytes(engine->hot) + doc_size > hot_tier_capacity_bytes(engine->hot)) {
        document_t* evicted = hot_tier_evict_one(engine->hot);
        if (!evicted) break;
        cold_tier_append(engine->cold, evicted);
        document_free(evicted);
    }
    
    bool ok = hot_tier_insert(engine->hot, doc);
    if (!ok) {
        //fallback directly to cold tier if hot tier cannot accept doc
        doc->location = TIER_COLD;
        ok = cold_tier_append(engine->cold, doc);
        document_free(doc);
    }
    
    pthread_rwlock_unlock(&engine->lock);
    return ok;
}

bool engine_del(calderadb_engine_t* engine, const char* key) {
    if (!engine || !key) return false;
    
    doc_id_t doc_id = doc_id_from_string(key);
    pthread_rwlock_wrlock(&engine->lock);
    __atomic_fetch_add(&engine->stats.total_dels, 1, __ATOMIC_RELAXED);
    
    bool removed = false;
    
    document_t* doc = hot_tier_get(engine->hot, &doc_id);
    if (doc) {
        hot_tier_remove(engine->hot, &doc_id);
        document_free(doc);
        removed = true;
    }
    
    if (cold_tier_mark_deleted(engine->cold, &doc_id)) {
        removed = true;
    }
    
    pthread_rwlock_unlock(&engine->lock);
    doc_id_free(&doc_id);
    return removed;
}

engine_stats_t engine_stats(calderadb_engine_t* engine) {
    engine_stats_t s = {0};
    if (engine) {
        pthread_rwlock_rdlock(&engine->lock);
        s = engine->stats;
        pthread_rwlock_unlock(&engine->lock);
    }
    return s;
}

hot_tier_t* engine_hot_tier(calderadb_engine_t* engine) {
    return engine ? engine->hot : NULL;
}

cold_tier_t* engine_cold_tier(calderadb_engine_t* engine) {
    return engine ? engine->cold : NULL;
}
