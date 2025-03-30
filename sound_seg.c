#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44

#pragma pack(push, 1)
struct sound_seg_node {
    union {
        struct {
            int16_t sample;      // 2 bytes
            uint16_t id;         // 2 bytes, unique for parents
            uint16_t refCount;   // 2 bytes (max 65,535 references)
        } parent_data;
        struct {
            uint16_t parent_id;  // 2 bytes, references parent's id
        } child_data;
    } A;
    struct sound_seg_node* next; // 8 bytes (64-bit system)
    bool isParent;               // 1 byte (padded)
};
#pragma pack(pop)

struct sound_seg {
    struct sound_seg_node* head;
};

// Global counter for assigning unique IDs (max 65,535)
static uint16_t next_id = 1;

// Helper function to find a node by ID
struct sound_seg_node* find_node_by_id(struct sound_seg* track, uint16_t id) {
    struct sound_seg_node* current = track->head;
    while (current) {
        if (current->isParent && current->A.parent_data.id == id) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// Helper function to update all nodes (parent and children) with a given ID
void update_related_nodes(struct sound_seg* track, uint16_t id, int16_t new_sample) {
    struct sound_seg_node* current = track->head;
    while (current) {
        if (current->isParent && current->A.parent_data.id == id) {
            current->A.parent_data.sample = new_sample;
        } else if (!current->isParent && current->A.child_data.parent_id == id) {
            // No sample to update in child, but we ensure consistency via parent
        }
        current = current->next;
    }
}

// Load a WAV file into buffer
void wav_load(const char* filename, int16_t* dest) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        printf("Error opening file\n");
        return;
    }
    fseek(file, OFFSET, SEEK_SET);
    uint32_t number_of_bytes;
    fread(&number_of_bytes, sizeof(uint32_t), 1, file);
    fseek(file, OFFSET_TO_AUDIO_DATA, SEEK_SET);
    fread(dest, sizeof(int16_t), number_of_bytes / sizeof(int16_t), file);
    fclose(file);
}

// Save a buffer to a WAV file
void wav_save(const char* fname, int16_t* src, size_t len) {
    FILE *file = fopen(fname, "wb");
    if (file == NULL) {
        printf("Error opening file\n");
        return;
    }
    union wav_header {
        struct {
            char riff[4];
            uint32_t flength;
            char wave[4];
            char fmt[4];
            int32_t chunk_size;
            int16_t format_tag;
            int16_t num_chans;
            int32_t sample_rate;
            int32_t bytes_per_sec;
            int16_t bytes_per_sample;
            int16_t bits_per_sample;
            char data[4];
            int32_t dlength;
        } fields;
        char bytes[OFFSET_TO_AUDIO_DATA];
    } header;
    memcpy(header.fields.riff, "RIFF", 4);
    header.fields.flength = len * sizeof(int16_t) + OFFSET_TO_AUDIO_DATA;
    memcpy(header.fields.wave, "WAVE", 4);
    memcpy(header.fields.fmt, "fmt ", 4);
    header.fields.chunk_size = 16;
    header.fields.format_tag = 1;
    header.fields.num_chans = 1;
    header.fields.sample_rate = 8000;
    header.fields.bytes_per_sec = 8000 * 2;
    header.fields.bytes_per_sample = 2;
    header.fields.bits_per_sample = 16;
    memcpy(header.fields.data, "data", 4);
    header.fields.dlength = len * sizeof(int16_t);
    fwrite(&header, sizeof(header), 1, file);
    fwrite(src, sizeof(int16_t), len, file);
    fclose(file);
}

// Initialize a track
struct sound_seg* tr_init(void) {
    struct sound_seg* track = malloc(sizeof(struct sound_seg));
    if (!track) return NULL;
    track->head = NULL;
    return track;
}

// Destroy a track
void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    struct sound_seg_node* current = track->head;
    while (current) {
        struct sound_seg_node* next = current->next;
        free(current);
        current = next;
    }
    free(track);
}

// Get track length
size_t tr_length(struct sound_seg* track) {
    size_t length = 0;
    struct sound_seg_node* current = track->head;
    while (current) {
        length++;
        current = current->next;
    }
    return length;
}

// Read samples from track
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest || len == 0) return;
    struct sound_seg_node* current = track->head;
    size_t i = 0;
    while (current && i < pos) {
        current = current->next;
        i++;
    }
    for (size_t j = 0; j < len && current; j++) {
        if (current->isParent) {
            dest[j] = current->A.parent_data.sample;
        } else {
            struct sound_seg_node* parent = find_node_by_id(track, current->A.child_data.parent_id);
            dest[j] = parent ? parent->A.parent_data.sample : 0;
        }
        current = current->next;
    }
}

// Write samples to track
void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;
    struct sound_seg_node* current = track->head;
    struct sound_seg_node* prev = NULL;
    size_t i = 0;

    while (current && i < pos) {
        prev = current;
        current = current->next;
        i++;
    }

    while (i < pos) {
        struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
        if (!new_node) return;
        new_node->A.parent_data.sample = 0;
        new_node->A.parent_data.id = next_id++;
        new_node->A.parent_data.refCount = 0;
        new_node->next = NULL;
        new_node->isParent = true;
        if (prev) prev->next = new_node;
        else track->head = new_node;
        prev = new_node;
        i++;
    }

    for (size_t j = 0; j < len; j++) {
        if (current) {
            if (current->isParent) {
                current->A.parent_data.sample = src[j];
                update_related_nodes(track, current->A.parent_data.id, src[j]);
            } else {
                struct sound_seg_node* parent = find_node_by_id(track, current->A.child_data.parent_id);
                if (parent) {
                    parent->A.parent_data.sample = src[j];
                    update_related_nodes(track, current->A.child_data.parent_id, src[j]);
                }
            }
            prev = current;
            current = current->next;
        } else {
            struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
            if (!new_node) return;
            new_node->A.parent_data.sample = src[j];
            new_node->A.parent_data.id = next_id++;
            new_node->A.parent_data.refCount = 0;
            new_node->next = NULL;
            new_node->isParent = true;
            if (prev) prev->next = new_node;
            else track->head = new_node;
            prev = new_node;
        }
    }
}

// Delete a range of nodes from track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || len == 0 || pos >= tr_length(track)) return false;
    struct sound_seg_node* current = track->head;
    struct sound_seg_node* prev = NULL;
    size_t i = 0;

    while (current && i < pos) {
        prev = current;
        current = current->next;
        i++;
    }
    if (!current) return false;

    struct sound_seg_node* check = current;
    for (size_t j = 0; j < len && check; j++) {
        if (check->isParent && check->A.parent_data.refCount > 0) {
            return false;
        }
        check = check->next;
    }

    for (size_t j = 0; j < len && current; j++) {
        struct sound_seg_node* next = current->next;
        if (!current->isParent) {
            struct sound_seg_node* parent = find_node_by_id(track, current->A.child_data.parent_id);
            if (parent && parent->A.parent_data.refCount > 0) {
                parent->A.parent_data.refCount--;
            }
        }
        free(current);
        current = next;
    }
    if (prev) prev->next = current;
    else track->head = current;
    return true;
}

// Compute cross-correlation between two buffers
double compute_cross_correlation(const int16_t* target, const int16_t* ad, size_t len) {
    double sum_product = 0.0;
    double sum_ad_sq = 0.0;
    for (size_t i = 0; i < len; i++) {
        sum_product += (double)target[i] * (double)ad[i];
        sum_ad_sq += (double)ad[i] * (double)ad[i];
    }
    return sum_product / sum_ad_sq;
}

// Identify matching segments in target track
char* tr_identify(struct sound_seg* target, struct sound_seg* ad) {
    if (!target || !ad || tr_length(ad) > tr_length(target)) return strdup("");
    size_t target_len = tr_length(target);
    size_t ad_len = tr_length(ad);
    int16_t* target_data = malloc(target_len * sizeof(int16_t));
    int16_t* ad_data = malloc(ad_len * sizeof(int16_t));
    if (!target_data || !ad_data) {
        free(target_data);
        free(ad_data);
        return strdup("");
    }
    tr_read(target, target_data, 0, target_len);
    tr_read(ad, ad_data, 0, ad_len);

    char* result = malloc(256);
    if (!result) {
        free(target_data);
        free(ad_data);
        return strdup("");
    }
    size_t capacity = 256;
    size_t used = 0;
    result[0] = '\0';

    for (size_t i = 0; i <= target_len - ad_len; i++) {
        double corr = compute_cross_correlation(target_data + i, ad_data, ad_len);
        if (corr >= 0.95) {
            char temp[32];
            snprintf(temp, sizeof(temp), "%zu,%zu\n", i, i + ad_len - 1);
            size_t len = strlen(temp);
            if (used + len + 1 > capacity) {
                capacity *= 2;
                char* new_result = realloc(result, capacity);
                if (!new_result) {
                    free(result);
                    free(target_data);
                    free(ad_data);
                    return strdup("");
                }
                result = new_result;
            }
            strcpy(result + used, temp);
            used += len;
            i += ad_len - 1;
        }
    }
    free(target_data);
    free(ad_data);
    if (used == 0) {
        free(result);
        return strdup("");
    }
    if (used > 0 && result[used - 1] == '\n') result[used - 1] = '\0';
    return result;
}

// Insert a segment from src_track to dest_track
void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track, size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || len == 0 || srcpos + len > tr_length(src_track)) return;
    struct sound_seg_node* src_current = src_track->head;
    size_t i = 0;
    while (src_current && i < srcpos) {
        src_current = src_current->next;
        i++;
    }
    if (!src_current) return;

    struct sound_seg_node* dest_current = dest_track->head;
    struct sound_seg_node* dest_prev = NULL;
    i = 0;
    while (dest_current && i < destpos) {
        dest_prev = dest_current;
        dest_current = dest_current->next;
        i++;
    }

    struct sound_seg_node* insert_head = NULL;
    struct sound_seg_node* insert_tail = NULL;
    struct sound_seg_node* src_temp = src_current;

    for (size_t j = 0; j < len && src_temp; j++) {
        struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
        if (!new_node) return;
        struct sound_seg_node* parent = src_temp;
        while (!parent->isParent) {
            parent = find_node_by_id(src_track, parent->A.child_data.parent_id);
            if (!parent) {
                free(new_node);
                return;
            }
        }
        new_node->A.child_data.parent_id = parent->A.parent_data.id;
        new_node->next = NULL;
        new_node->isParent = false;
        parent->A.parent_data.refCount++;
        if (!insert_head) insert_head = insert_tail = new_node;
        else {
            insert_tail->a = new_node;
            insert_tail = new_node;
        }
        src_temp = src_temp->next;
    }

    if (insert_head) {
        insert_tail->next = dest_current;
        if (dest_prev) dest_prev->next = insert_head;
        else dest_track->head = insert_head;
    }
}

int main(int argc, char** argv) {
    return 0;
}