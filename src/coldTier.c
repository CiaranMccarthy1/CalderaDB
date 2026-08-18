#include "calderadb/cold/coldTier.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* Index entry */
typedef struct {
    uint64_t offset;
    uint32_t payload_len;
    uint32_t version;
    timestamp_t modified_at;
} index_entry_t;

struct cold_tier {
    char data_path[256];
    char index_path[256];
    FILE* data_fp;
    FILE* index_fp;
    hashtable_t* index;
    size_t total_bytes;
    size_t doc_count;
    uint64_t current_offset;
};

static uint64_t xxhash64(const uint8_t* data, size_t len) {
    /* Simple hash for MVP - in production use xxHash */
    uint64_t hash = 0x9e3779b97f4a7c15ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 0x9e3779b97f4a7c15ULL;
        hash = (hash << 31) | (hash >> 33);
    }
    return hash;
}

cold_tier_t* cold_tier_create(const char* data_dir) {
    if (!data_dir) return NULL;
    
    cold_tier_t* tier = calloc(1, sizeof(cold_tier_t));
    if (!tier) return NULL;
    
    snprintf(tier->data_path, sizeof(tier->data_path), "%s/data.flux", data_dir);
    snprintf(tier->index_path, sizeof(tier->index_path), "%s/index.flux", data_dir);
    
    /* Create directory if it doesn't exist */
    struct stat st;
    if (stat(data_dir, &st) != 0) {
        mkdir(data_dir, 0755);
    }
    
    /* Open data file (append mode) */
    tier->data_fp = fopen(tier->data_path, "ab+");
    if (!tier->data_fp) {
        free(tier);
        return NULL;
    }
    
    /* Open index file */
    tier->index_fp = fopen(tier->index_path, "ab+");
    if (!tier->index_fp) {
        fclose(tier->data_fp);
        free(tier);
        return NULL;
    }
    
    tier->index = hashtable_create(1024);
    if (!tier->index) {
        fclose(tier->data_fp);
        fclose(tier->index_fp);
        free(tier);
        return NULL;
    }
    
    tier->total_bytes = 0;
    tier->doc_count = 0;
    tier->current_offset = 0;
    
    /* Recover index from data file */
    cold_tier_recover(tier);
    
    return tier;
}

void cold_tier_destroy(cold_tier_t* tier) {
    if (!tier) return;
    
    fclose(tier->data_fp);
    fclose(tier->index_fp);
    hashtable_destroy(tier->index);
    free(tier);
}

bool cold_tier_append(cold_tier_t* tier, document_t* doc) {
    if (!tier || !doc) return false;
    
    uint32_t id_len = doc->id.len;
    uint32_t payload_len = doc->payload.len;
    uint64_t checksum = xxhash64(doc->payload.data, doc->payload.len);
    
    /* Write to data file */
    fwrite(&id_len, sizeof(uint32_t), 1, tier->data_fp);
    fwrite(doc->id.data, id_len, 1, tier->data_fp);
    fwrite(&payload_len, sizeof(uint32_t), 1, tier->data_fp);
    fwrite(doc->payload.data, payload_len, 1, tier->data_fp);
    fwrite(&checksum, sizeof(uint64_t), 1, tier->data_fp);
    fflush(tier->data_fp);
    
    /* Update index */
    index_entry_t* entry = malloc(sizeof(index_entry_t));
    if (!entry) return false;
    
    entry->offset = tier->current_offset;
    entry->payload_len = payload_len;
    entry->version = doc->version;
    entry->modified_at = doc->modified_at;
    
    doc_id_t key = {.data = strdup(doc->id.data), .len = doc->id.len};
    if (!hashtable_insert(tier->index, &key, (document_t*)entry)) {
        free(entry);
        doc_id_free(&key);
        return false;
    }
    
    tier->current_offset += 4 + id_len + 4 + payload_len + 8;
    tier->total_bytes += payload_len;
    tier->doc_count++;
    
    return true;
}

document_t* cold_tier_read(cold_tier_t* tier, const doc_id_t* id) {
    if (!tier || !id) return NULL;
    
    index_entry_t* entry = (index_entry_t*)hashtable_lookup(tier->index, id);
    if (!entry) return NULL;
    
    /* Read from data file */
    fseek(tier->data_fp, entry->offset, SEEK_SET);
    
    uint32_t id_len;
    fread(&id_len, sizeof(uint32_t), 1, tier->data_fp);
    
    char* id_data = malloc(id_len + 1);
    fread(id_data, id_len, 1, tier->data_fp);
    id_data[id_len] = '\0';
    
    uint32_t payload_len;
    fread(&payload_len, sizeof(uint32_t), 1, tier->data_fp);
    
    uint8_t* payload = malloc(payload_len);
    fread(payload, payload_len, 1, tier->data_fp);
    
    uint64_t stored_checksum;
    fread(&stored_checksum, sizeof(uint64_t), 1, tier->data_fp);
    
    /* Verify checksum */
    uint64_t computed_checksum = xxhash64(payload, payload_len);
    if (stored_checksum != computed_checksum) {
        free(id_data);
        free(payload);
        return NULL;
    }
    
    /* Create document */
    document_t* doc = document_create(id_data, payload, payload_len);
    free(id_data);
    free(payload);
    
    doc->version = entry->version;
    doc->modified_at = entry->modified_at;
    doc->location = TIER_COLD;
    
    return doc;
}

bool cold_tier_mark_deleted(cold_tier_t* tier, const doc_id_t* id) {
    if (!tier || !id) return false;
    /* Soft delete - just remove from index */
    return hashtable_remove(tier->index, id);
}

size_t cold_tier_total_bytes(const cold_tier_t* tier) {
    return tier ? tier->total_bytes : 0;
}

size_t cold_tier_doc_count(const cold_tier_t* tier) {
    return tier ? tier->doc_count : 0;
}

bool cold_tier_recover(cold_tier_t* tier) {
    if (!tier) return false;
    
    fseek(tier->data_fp, 0, SEEK_END);
    size_t file_size = ftell(tier->data_fp);
    if (file_size == 0) return true;
    
    fseek(tier->data_fp, 0, SEEK_SET);
    uint64_t offset = 0;
    
    while (offset < file_size) {
        uint32_t id_len;
        if (fread(&id_len, sizeof(uint32_t), 1, tier->data_fp) != 1) break;
        
        char* id_data = malloc(id_len + 1);
        if (!id_data) break;
        if (fread(id_data, id_len, 1, tier->data_fp) != 1) {
            free(id_data);
            break;
        }
        id_data[id_len] = '\0';
        
        uint32_t payload_len;
        if (fread(&payload_len, sizeof(uint32_t), 1, tier->data_fp) != 1) {
            free(id_data);
            break;
        }
        
        fseek(tier->data_fp, payload_len, SEEK_CUR);
        
        uint64_t checksum;
        if (fread(&checksum, sizeof(uint64_t), 1, tier->data_fp) != 1) {
            free(id_data);
            break;
        }
        
        /* Add to index */
        index_entry_t* entry = malloc(sizeof(index_entry_t));
        entry->offset = offset;
        entry->payload_len = payload_len;
        entry->version = 1;
        entry->modified_at = 0;
        
        doc_id_t key = {.data = id_data, .len = id_len};
        hashtable_insert(tier->index, &key, (document_t*)entry);
        
        tier->current_offset = offset + 4 + id_len + 4 + payload_len + 8;
        tier->total_bytes += payload_len;
        tier->doc_count++;
        offset = tier->current_offset;
    }
    
    return true;
}

bool cold_tier_compact(cold_tier_t* tier) {
    if (!tier) return false;
    
    char tmp_path[264]; // ponytail: 256 + ".tmp"
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", tier->data_path);
    FILE* tmp_fp = fopen(tmp_path, "wb");
    if (!tmp_fp) return false;

    // ponytail: single-pass compaction, no incremental/background, just rewrite the whole file
    uint64_t new_offset = 0;
    
    ht_iter_t* it = hashtable_iter_create(tier->index);
    if (!it) {
        fclose(tmp_fp);
        remove(tmp_path);
        return false;
    }
    
    doc_id_t key;
    document_t* val;
    while (hashtable_iter_next(it, &key, &val)) {
        index_entry_t* entry = (index_entry_t*)val;
        
        fseek(tier->data_fp, entry->offset, SEEK_SET);
        uint32_t id_len;
        if (fread(&id_len, sizeof(uint32_t), 1, tier->data_fp) != 1) continue;
        
        char* id_data = malloc(id_len);
        if (fread(id_data, id_len, 1, tier->data_fp) != 1) { free(id_data); continue; }
        
        uint32_t payload_len;
        if (fread(&payload_len, sizeof(uint32_t), 1, tier->data_fp) != 1) { free(id_data); continue; }
        
        uint8_t* payload = malloc(payload_len);
        if (fread(payload, payload_len, 1, tier->data_fp) != 1) { free(id_data); free(payload); continue; }
        
        uint64_t stored_checksum;
        if (fread(&stored_checksum, sizeof(uint64_t), 1, tier->data_fp) != 1) { free(id_data); free(payload); continue; }
        
        uint64_t current_entry_offset = new_offset;
        fwrite(&id_len, sizeof(uint32_t), 1, tmp_fp);
        fwrite(id_data, id_len, 1, tmp_fp);
        fwrite(&payload_len, sizeof(uint32_t), 1, tmp_fp);
        fwrite(payload, payload_len, 1, tmp_fp);
        fwrite(&stored_checksum, sizeof(uint64_t), 1, tmp_fp);
        
        entry->offset = current_entry_offset;
        new_offset += 4 + id_len + 4 + payload_len + 8;
        
        free(id_data);
        free(payload);
    }
    hashtable_iter_destroy(it);
    
    fflush(tmp_fp);
    fclose(tmp_fp);
    fclose(tier->data_fp);
    rename(tmp_path, tier->data_path);
    tier->data_fp = fopen(tier->data_path, "ab+");
    tier->current_offset = new_offset;
    
    return true;
}