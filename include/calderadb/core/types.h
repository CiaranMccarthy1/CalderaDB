#ifndef CALDERADB_TYPES_H
#define CALDERADB_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Basic types */
typedef uint64_t timestamp_t;
typedef uint32_t collection_id_t;
typedef uint32_t version_t;
typedef uint64_t checksum_t;

/* Document ID */
typedef struct {
    char* data;
    size_t len;
} doc_id_t;

/* Binary payload */
typedef struct {
    uint8_t* data;
    size_t len;
} binary_payload_t;

/* Tier enum */
typedef enum {
    TIER_HOT = 0,
    TIER_COLD = 1
} tier_t;

/* Document structure */
typedef struct document {
    doc_id_t id;
    collection_id_t collection_id;
    binary_payload_t payload;
    version_t version;
    timestamp_t created_at;
    timestamp_t modified_at;
    checksum_t checksum;
    tier_t location;
    uint32_t access_count;
    timestamp_t last_accessed;
    size_t size_bytes;
    struct document* next;
} document_t;

/* Helper functions */
doc_id_t doc_id_from_string(const char* str);
void doc_id_free(doc_id_t* id);
binary_payload_t payload_from_bytes(const uint8_t* data, size_t len);
void payload_free(binary_payload_t* payload);
document_t* document_create(const char* id, const uint8_t* data, size_t len);
void document_free(document_t* doc);

#endif /* CALDERADB_TYPES_H */