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

// ponytail: real xxHash64 in ~30 lines
#define XXH_PRIME64_1 0x9E3779B185EBCA87ULL
#define XXH_PRIME64_2 0xC2B2AE3D27D4EB4FULL
#define XXH_PRIME64_3 0x165667B19E3779F9ULL
#define XXH_PRIME64_4 0x85EBCA77C2B2AE63ULL
#define XXH_PRIME64_5 0x27D4EB2F165667C5ULL

static inline uint64_t xxh_rotl64(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
static inline uint64_t xxh_round(uint64_t acc, uint64_t input) {
    return xxh_rotl64(acc + input * XXH_PRIME64_2, 31) * XXH_PRIME64_1;
}

static uint64_t xxhash64(const uint8_t* data, size_t len) {
    const uint8_t* p = data;
    const uint8_t* const bEnd = data + len;
    uint64_t h64;

    if (len >= 32) {
        const uint8_t* const limit = bEnd - 32;
        uint64_t v1 = XXH_PRIME64_1 + XXH_PRIME64_2;
        uint64_t v2 = XXH_PRIME64_2;
        uint64_t v3 = 0;
        uint64_t v4 = (uint64_t)0 - XXH_PRIME64_1;
        do {
            uint64_t k1, k2, k3, k4;
            memcpy(&k1, p, 8); memcpy(&k2, p + 8, 8);
            memcpy(&k3, p + 16, 8); memcpy(&k4, p + 24, 8);
            v1 = xxh_round(v1, k1); v2 = xxh_round(v2, k2);
            v3 = xxh_round(v3, k3); v4 = xxh_round(v4, k4);
            p += 32;
        } while (p <= limit);
        h64 = xxh_rotl64(v1, 1) + xxh_rotl64(v2, 7) + xxh_rotl64(v3, 12) + xxh_rotl64(v4, 18);
        h64 = (h64 ^ xxh_round(0, v1)) * XXH_PRIME64_1 + XXH_PRIME64_4;
        h64 = (h64 ^ xxh_round(0, v2)) * XXH_PRIME64_1 + XXH_PRIME64_4;
        h64 = (h64 ^ xxh_round(0, v3)) * XXH_PRIME64_1 + XXH_PRIME64_4;
        h64 = (h64 ^ xxh_round(0, v4)) * XXH_PRIME64_1 + XXH_PRIME64_4;
    } else {
        h64 = XXH_PRIME64_5;
    }
    h64 += (uint64_t)len;

    while (p + 8 <= bEnd) {
        uint64_t k1;
        memcpy(&k1, p, 8);
        h64 ^= xxh_round(0, k1);
        h64 = xxh_rotl64(h64, 27) * XXH_PRIME64_1 + XXH_PRIME64_4;
        p += 8;
    }
    if (p + 4 <= bEnd) {
        uint32_t k1;
        memcpy(&k1, p, 4);
        h64 ^= (uint64_t)(k1) * XXH_PRIME64_1;
        h64 = xxh_rotl64(h64, 23) * XXH_PRIME64_2 + XXH_PRIME64_3;
        p += 4;
    }
    while (p < bEnd) {
        h64 ^= (*p) * XXH_PRIME64_5;
        h64 = xxh_rotl64(h64, 11) * XXH_PRIME64_1;
        p++;
    }

    h64 ^= h64 >> 33;
    h64 *= XXH_PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= XXH_PRIME64_3;
    h64 ^= h64 >> 32;
    return h64;
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
    
    if (tier->index) {
        ht_iter_t* iter = hashtable_iter_create(tier->index);
        if (iter) {
            doc_id_t key;
            document_t* val;
            while (hashtable_iter_next(iter, &key, &val)) {
                free(val); // index_entry_t
            }
            hashtable_iter_destroy(iter);
        }
        hashtable_destroy(tier->index);
    }
    if (tier->data_fp) fclose(tier->data_fp);
    if (tier->index_fp) fclose(tier->index_fp);
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
    
    // Free existing index entry if overwriting
    index_entry_t* existing = (index_entry_t*)hashtable_lookup(tier->index, &doc->id);
    if (existing) {
        if (tier->total_bytes >= existing->payload_len) tier->total_bytes -= existing->payload_len;
        free(existing);
    } else {
        tier->doc_count++;
    }
    
    if (!hashtable_insert(tier->index, &doc->id, (document_t*)entry)) {
        free(entry);
        return false;
    }
    
    tier->current_offset += 4 + id_len + 4 + payload_len + 8;
    tier->total_bytes += payload_len;
    
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
    
    index_entry_t* entry = (index_entry_t*)hashtable_lookup(tier->index, id);
    if (!entry) return false;
    
    // ponytail: persist tombstone with 0xFFFFFFFF payload_len
    uint32_t id_len = (uint32_t)id->len;
    uint32_t payload_len = 0xFFFFFFFF;
    uint64_t checksum = 0;
    fwrite(&id_len, sizeof(uint32_t), 1, tier->data_fp);
    fwrite(id->data, id_len, 1, tier->data_fp);
    fwrite(&payload_len, sizeof(uint32_t), 1, tier->data_fp);
    fwrite(&checksum, sizeof(uint64_t), 1, tier->data_fp);
    fflush(tier->data_fp);

    tier->current_offset += 4 + id_len + 4 + 8;
    if (tier->doc_count > 0) tier->doc_count--;
    if (tier->total_bytes >= entry->payload_len) tier->total_bytes -= entry->payload_len;

    free(entry);
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
        doc_id_t key = {.data = id_data, .len = id_len};
        
        uint32_t payload_len;
        if (fread(&payload_len, sizeof(uint32_t), 1, tier->data_fp) != 1) {
            free(id_data);
            break;
        }
        
        if (payload_len == 0xFFFFFFFF) {
            // Tombstone record: remove from index on recovery
            uint64_t checksum;
            if (fread(&checksum, sizeof(uint64_t), 1, tier->data_fp) != 1) {
                free(id_data);
                break;
            }
            index_entry_t* existing = (index_entry_t*)hashtable_lookup(tier->index, &key);
            if (existing) {
                if (tier->doc_count > 0) tier->doc_count--;
                if (tier->total_bytes >= existing->payload_len) tier->total_bytes -= existing->payload_len;
                free(existing);
                hashtable_remove(tier->index, &key);
            }
            free(id_data);
            offset += 4 + id_len + 4 + 8;
            tier->current_offset = offset;
            continue;
        }
        
        fseek(tier->data_fp, payload_len, SEEK_CUR);
        
        uint64_t checksum;
        if (fread(&checksum, sizeof(uint64_t), 1, tier->data_fp) != 1) {
            free(id_data);
            break;
        }
        
        index_entry_t* existing = (index_entry_t*)hashtable_lookup(tier->index, &key);
        if (existing) {
            if (tier->total_bytes >= existing->payload_len) tier->total_bytes -= existing->payload_len;
            free(existing);
        } else {
            tier->doc_count++;
        }
        
        /* Add to index */
        index_entry_t* entry = malloc(sizeof(index_entry_t));
        entry->offset = offset;
        entry->payload_len = payload_len;
        entry->version = 1;
        entry->modified_at = 0;
        
        hashtable_insert(tier->index, &key, (document_t*)entry);
        free(id_data);
        
        offset += 4 + id_len + 4 + payload_len + 8;
        tier->total_bytes += payload_len;
        tier->current_offset = offset;
    }
    
    return true;
}

bool cold_tier_compact(cold_tier_t* tier) {
    if (!tier) return false;
    
    char tmp_path[264]; // ponytail: 256 + ".tmp"
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", tier->data_path);
    FILE* tmp_fp = fopen(tmp_path, "wb");
    if (!tmp_fp) return false;

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