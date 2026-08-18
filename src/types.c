#include "calderadb/core/types.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

doc_id_t doc_id_from_string(const char* str) {
    doc_id_t id;
    id.len = strlen(str);
    id.data = malloc(id.len + 1);
    memcpy(id.data, str, id.len + 1);
    return id;
}

void doc_id_free(doc_id_t* id) {
    free(id->data);
    id->data = NULL;
    id->len = 0;
}

binary_payload_t payload_from_bytes(const uint8_t* data, size_t len) {
    binary_payload_t p;
    p.len = len;
    p.data = malloc(len);
    memcpy(p.data, data, len);
    return p;
}

void payload_free(binary_payload_t* payload) {
    free(payload->data);
    payload->data = NULL;
    payload->len = 0;
}

document_t* document_create(const char* id, const uint8_t* data, size_t len) {
    document_t* doc = calloc(1, sizeof(document_t));
    doc->id = doc_id_from_string(id);
    doc->payload = payload_from_bytes(data, len);
    doc->size_bytes = len;
    doc->created_at = (timestamp_t)time(NULL);
    doc->modified_at = doc->created_at;
    return doc;
}

void document_free(document_t* doc) {
    if (!doc) return;
    doc_id_free(&doc->id);
    payload_free(&doc->payload);
    free(doc);
}
