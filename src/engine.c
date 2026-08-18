#include "calderadb/engine/engine.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct calderadb_engine {
    hot_tier_t* hot;
    cold_tier_t* cold;
    engine_stats_t stats;
    pthread_mutex_t lock;
};

calderadb_engine_t* engine_create(size_t hot_capacity, const char* data_dir) {
    calderadb_engine_t* engine = calloc(1, sizeof(calderadb_engine_t));
    if (!engine) return NULL;
    
    engine->hot = hot_tier_create(hot_capacity);
    if (!engine->hot) {
        free(engine);
        return NULL;
    }
    
    engine->cold = cold_tier_create(data_dir);
    if (!engine->cold) {
        hot_tier_destroy(engine->hot);
        free(engine);
        return NULL;
    }
    
    pthread_mutex_init(&engine->lock, NULL);
    return engine;
}

void engine_destroy(calderadb_engine_t* engine) {
    if (!engine) return;
    hot_tier_destroy(engine->hot);
    cold_tier_destroy(engine->cold);
    pthread_mutex_destroy(&engine->lock);
    free(engine);
}

document_t* engine_get(calderadb_engine_t* engine, const char* key) {
    if (!engine || !key) return NULL;
    
    doc_id_t doc_id = doc_id_from_string(key);
    pthread_mutex_lock(&engine->lock);
    engine->stats.total_gets++;
    
    document_t* doc = hot_tier_get(engine->hot, &doc_id);
    if (doc) {
        engine->stats.hot_hits++;
        pthread_mutex_unlock(&engine->lock);
        doc_id_free(&doc_id);
        return doc;
    }
    
    engine->stats.misses++; // Will override if found in cold
    
    doc = cold_tier_read(engine->cold, &doc_id);
    if (doc) {
        engine->stats.misses--;
        engine->stats.cold_hits++;
        
        // Promote to hot
        size_t doc_size = doc->size_bytes + sizeof(doc_id_t) + sizeof(document_t);
        while (hot_tier_used_bytes(engine->hot) + doc_size > hot_tier_capacity_bytes(engine->hot)) {
            document_t* evicted = hot_tier_evict_one(engine->hot);
            if (!evicted) break; // Should not happen
            cold_tier_append(engine->cold, evicted);
            document_free(evicted);
        }
        hot_tier_insert(engine->hot, doc);
        // doc is now in hot tier, caller still uses returned pointer (which is same).
    }
    
    pthread_mutex_unlock(&engine->lock);
    doc_id_free(&doc_id);
    return doc;
}

bool engine_set(calderadb_engine_t* engine, const char* key, const uint8_t* value, size_t value_len) {
    if (!engine || !key || !value) return false;
    
    document_t* doc = document_create(key, value, value_len);
    if (!doc) return false;
    
    pthread_mutex_lock(&engine->lock);
    engine->stats.total_sets++;
    
    size_t doc_size = doc->size_bytes + sizeof(doc_id_t) + sizeof(document_t);
    while (hot_tier_used_bytes(engine->hot) + doc_size > hot_tier_capacity_bytes(engine->hot)) {
        document_t* evicted = hot_tier_evict_one(engine->hot);
        if (!evicted) break;
        cold_tier_append(engine->cold, evicted);
        document_free(evicted);
    }
    
    bool ok = hot_tier_insert(engine->hot, doc);
    if (!ok) {
        document_free(doc);
    }
    
    pthread_mutex_unlock(&engine->lock);
    return ok;
}

bool engine_del(calderadb_engine_t* engine, const char* key) {
    if (!engine || !key) return false;
    
    doc_id_t doc_id = doc_id_from_string(key);
    pthread_mutex_lock(&engine->lock);
    engine->stats.total_dels++;
    
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
    
    pthread_mutex_unlock(&engine->lock);
    doc_id_free(&doc_id);
    return removed;
}

engine_stats_t engine_stats(calderadb_engine_t* engine) {
    engine_stats_t s = {0};
    if (engine) {
        pthread_mutex_lock(&engine->lock);
        s = engine->stats;
        pthread_mutex_unlock(&engine->lock);
    }
    return s;
}

hot_tier_t* engine_hot_tier(calderadb_engine_t* engine) {
    return engine ? engine->hot : NULL;
}

cold_tier_t* engine_cold_tier(calderadb_engine_t* engine) {
    return engine ? engine->cold : NULL;
}
