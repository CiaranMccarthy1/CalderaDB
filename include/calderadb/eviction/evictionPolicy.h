#ifndef CALDERADB_EVICTION_POLICY_H
#define CALDERADB_EVICTION_POLICY_H

#include <stdbool.h>
#include <stdint.h>
#include "calderadb/core/types.h"

/* Forward declarations */
typedef struct eviction_policy eviction_policy_t;

/* Function pointer types */
typedef void (*eviction_record_access_fn)(eviction_policy_t* policy, document_t* doc);
typedef bool (*eviction_should_demote_fn)(eviction_policy_t* policy, document_t* doc, timestamp_t now);
typedef void (*eviction_evaluate_fn)(eviction_policy_t* policy, document_t** docs, size_t count, timestamp_t now);
typedef void (*eviction_reset_fn)(eviction_policy_t* policy, document_t* doc);
typedef void (*eviction_destroy_fn)(eviction_policy_t* policy);

/* Policy vtable */
typedef struct {
    eviction_record_access_fn record_access;
    eviction_should_demote_fn should_demote;
    eviction_evaluate_fn evaluate;
    eviction_reset_fn reset;
    eviction_destroy_fn destroy;
} eviction_vtable_t;

/* Base policy struct */
struct eviction_policy {
    const eviction_vtable_t* vtable;
};

/* Helper to invoke methods */
static inline void eviction_record_access(eviction_policy_t* policy, document_t* doc) {
    policy->vtable->record_access(policy, doc);
}

static inline bool eviction_should_demote(eviction_policy_t* policy, document_t* doc, timestamp_t now) {
    return policy->vtable->should_demote(policy, doc, now);
}

static inline void eviction_evaluate(eviction_policy_t* policy, document_t** docs, size_t count, timestamp_t now) {
    policy->vtable->evaluate(policy, docs, count, now);
}

static inline void eviction_reset(eviction_policy_t* policy, document_t* doc) {
    policy->vtable->reset(policy, doc);
}

static inline void eviction_destroy(eviction_policy_t* policy) {
    policy->vtable->destroy(policy);
}

/* Sliding window policy */
typedef struct sliding_window_config {
    uint32_t x;              /* Min access count */
    uint64_t y;              /* Time window (ms) */
    uint64_t eval_interval;  /* Evaluation interval (ms) */
} sliding_window_config_t;

eviction_policy_t* sliding_window_policy_create(sliding_window_config_t config);

/* LRU policy */
eviction_policy_t* lru_policy_create(void);

/* LFU policy */
eviction_policy_t* lfu_policy_create(void);

/* TTL policy */
eviction_policy_t* ttl_policy_create(uint64_t ttl_ms);

#endif /* CALDERADB_EVICTION_POLICY_H */