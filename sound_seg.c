#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44
#define MAX_NODES 1024
struct data {
    int16_t *audio_data;
    size_t capacity;
};

// 12-byte node structure (32-bit system)
#pragma pack(push, 1)
struct sound_seg_node {
    struct data *data;    // 4 bytes
    uint16_t start;       // 2 bytes
    uint16_t length;      // 2 bytes
    uint8_t refCount;     // 1 byte
    bool is_child;        // 1 byte
    uint16_t next_idx;    // 2 bytes (index to next node in array, not a pointer)
};                        // Total: 12 bytes
#pragma pack(pop)

struct sound_seg {
    struct sound_seg_node *nodes; // Array of nodes
    size_t node_count;            // Current number of nodes
    size_t node_capacity;         // Total allocated nodes
    struct data *shared_data;     // Single shared buffer
};

// Helper to add a node to the array
static size_t add_node(struct sound_seg *track, struct data *data, size_t start, size_t length, bool is_child) {
    if (track->node_count >= track->node_capacity) {
        size_t new_capacity = track->node_capacity ? track->node_capacity * 2 : MAX_NODES;
        struct sound_seg_node *new_nodes = (struct sound_seg_node *)realloc(track->nodes, new_capacity * sizeof(struct sound_seg_node));
        if (!new_nodes) return (size_t)-1;
        track->nodes = new_nodes;
        track->node_capacity = new_capacity;
    }
    size_t idx = track->node_count++;
    track->nodes[idx].data = data;
    track->nodes[idx].start = start;
    track->nodes[idx].length = length;
    track->nodes[idx].refCount = 0;
    track->nodes[idx].is_child = is_child;
    track->nodes[idx].next_idx = (uint16_t)-1; // No next node
    return idx;
}

void wav_load(const char* filename, int16_t* dest) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
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

void wav_save(const char* fname, int16_t* src, size_t len) {
    FILE *file = fopen(fname, "wb");
    if (!file) {
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
    header.fields.flength = len * sizeof(int16_t) + OFFSET_TO_AUDIO_DATA - 8;
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

struct sound_seg* tr_init(void) {
    struct sound_seg *track = (struct sound_seg *)malloc(sizeof(struct sound_seg));
    if (!track) return NULL;
    track->nodes = NULL;
    track->node_count = 0;
    track->node_capacity = 0;
    track->shared_data = (struct data *)malloc(sizeof(struct data));
    if (!track->shared_data) {
        free(track);
        return NULL;
    }
    track->shared_data->audio_data = NULL;
    track->shared_data->capacity = 0;
    return track;
}

void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    for (size_t i = 0; i < track->node_count; i++) {
        if (!track->nodes[i].is_child) {
            free(track->nodes[i].data->audio_data);
            free(track->nodes[i].data);
        }
    }
    free(track->nodes);
    if (track->shared_data) {
        free(track->shared_data->audio_data);
        free(track->shared_data);
    }
    free(track);
}

size_t tr_length(struct sound_seg* track) {
    if (!track || !track->node_count) return 0;
    size_t length = 0;
    size_t idx = 0;
    while (idx != (size_t)-1 && idx < track->node_count) {
        length += track->nodes[idx].length;
        idx = track->nodes[idx].next_idx;
    }
    return length;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !track->node_count || !dest || len == 0) return;
    size_t offset = 0;
    size_t idx = 0;
    while (idx != (size_t)-1 && idx < track->node_count && offset + track->nodes[idx].length <= pos) {
        offset += track->nodes[idx].length;
        idx = track->nodes[idx].next_idx;
    }
    if (idx == (size_t)-1 || idx >= track->node_count) return;

    size_t remaining = len;
    size_t start_offset = pos - offset;
    int16_t *dest_ptr = dest;

    while (idx != (size_t)-1 && idx < track->node_count && remaining > 0) {
        size_t copy_len = (track->nodes[idx].length - start_offset) > remaining ? remaining : (track->nodes[idx].length - start_offset);
        memcpy(dest_ptr, track->nodes[idx].data->audio_data + track->nodes[idx].start + start_offset, copy_len * sizeof(int16_t));
        dest_ptr += copy_len;
        remaining -= copy_len;
        idx = track->nodes[idx].next_idx;
        start_offset = 0;
    }
}

void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;

    size_t total_len = pos + len;
    if (!track->shared_data->audio_data || total_len > track->shared_data->capacity) {
        size_t new_capacity = total_len > track->shared_data->capacity * 2 ? total_len : track->shared_data->capacity * 2;
        int16_t *new_data = (int16_t *)realloc(track->shared_data->audio_data, new_capacity * sizeof(int16_t));
        if (!new_data) return;
        track->shared_data->audio_data = new_data;
        track->shared_data->capacity = new_capacity;
    }

    if (!track->node_count) {
        size_t idx = add_node(track, track->shared_data, 0, total_len, false);
        if (idx == (size_t)-1) return;
        memcpy(track->shared_data->audio_data + pos, src, len * sizeof(int16_t));
        return;
    }

    size_t prev_idx = (size_t)-1;
    size_t curr_idx = 0;
    size_t offset = 0;

    while (curr_idx != (size_t)-1 && curr_idx < track->node_count && offset + track->nodes[curr_idx].length <= pos) {
        offset += track->nodes[curr_idx].length;
        prev_idx = curr_idx;
        curr_idx = track->nodes[curr_idx].next_idx;
    }

    if (curr_idx == (size_t)-1) {
        size_t new_idx = add_node(track, track->shared_data, offset, total_len - offset, false);
        if (new_idx == (size_t)-1) return;
        if (prev_idx != (size_t)-1) track->nodes[prev_idx].next_idx = new_idx;
        memcpy(track->shared_data->audio_data + pos, src, len * sizeof(int16_t));
        return;
    }

    size_t write_start = pos - offset;
    if (write_start > 0) {
        size_t right_idx = add_node(track, track->nodes[curr_idx].data, track->nodes[curr_idx].start + write_start, track->nodes[curr_idx].length - write_start, track->nodes[curr_idx].is_child);
        if (right_idx == (size_t)-1) return;
        track->nodes[right_idx].refCount = track->nodes[curr_idx].refCount;
        track->nodes[curr_idx].length = write_start;
        track->nodes[right_idx].next_idx = track->nodes[curr_idx].next_idx;
        track->nodes[curr_idx].next_idx = right_idx;
        prev_idx = curr_idx;
        curr_idx = right_idx;
    }

    size_t remaining = len;
    while (remaining > 0 && curr_idx != (size_t)-1 && curr_idx < track->node_count) {
        size_t copy_len = track->nodes[curr_idx].length < remaining ? track->nodes[curr_idx].length : remaining;
        memcpy(track->nodes[curr_idx].data->audio_data + track->nodes[curr_idx].start, src, copy_len * sizeof(int16_t));
        src += copy_len;
        remaining -= copy_len;
        prev_idx = curr_idx;
        curr_idx = track->nodes[curr_idx].next_idx;
    }

    if (remaining > 0) {
        size_t new_idx = add_node(track, track->shared_data, offset + tr_length(track) - remaining, remaining, false);
        if (new_idx == (size_t)-1) return;
        track->nodes[prev_idx].next_idx = new_idx;
        memcpy(track->nodes[new_idx].data->audio_data + track->nodes[new_idx].start, src, remaining * sizeof(int16_t));
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || !track->node_count || len == 0) return false;

    size_t prev_idx = (size_t)-1;
    size_t curr_idx = 0;
    size_t offset = 0;

    while (curr_idx != (size_t)-1 && curr_idx < track->node_count && offset + track->nodes[curr_idx].length <= pos) {
        offset += track->nodes[curr_idx].length;
        prev_idx = curr_idx;
        curr_idx = track->nodes[curr_idx].next_idx;
    }
    if (curr_idx == (size_t)-1 || curr_idx >= track->node_count) return false;

    size_t remaining = len;
    size_t delete_start = pos - offset;

    while (curr_idx != (size_t)-1 && curr_idx < track->node_count && remaining > 0) {
        if (!track->nodes[curr_idx].is_child && track->nodes[curr_idx].refCount > 0) return false;

        if (delete_start > 0 && delete_start < track->nodes[curr_idx].length) {
            size_t right_idx = add_node(track, track->nodes[curr_idx].data, track->nodes[curr_idx].start + delete_start + remaining, track->nodes[curr_idx].length - delete_start - remaining, track->nodes[curr_idx].is_child);
            if (right_idx == (size_t)-1) return false;
            track->nodes[right_idx].refCount = track->nodes[curr_idx].refCount;
            track->nodes[curr_idx].length = delete_start;
            track->nodes[right_idx].next_idx = track->nodes[curr_idx].next_idx;
            track->nodes[curr_idx].next_idx = right_idx;
            remaining = 0;
        } else if (delete_start == 0) {
            size_t delete_len = remaining > track->nodes[curr_idx].length ? track->nodes[curr_idx].length : remaining;
            track->nodes[curr_idx].start += delete_len;
            track->nodes[curr_idx].length -= delete_len;
            if (track->nodes[curr_idx].length == 0) {
                if (prev_idx != (size_t)-1) track->nodes[prev_idx].next_idx = track->nodes[curr_idx].next_idx;
                else curr_idx = track->nodes[curr_idx].next_idx; // Head removal
            }
            remaining -= delete_len;
        }
        prev_idx = curr_idx;
        curr_idx = track->nodes[curr_idx].next_idx;
    }
    return true;
}

double compute_cross_correlation(const int16_t* target, const int16_t* ad, size_t len) {
    double sum_product = 0.0;
    double sum_ad_sq = 0.0;
    for (size_t i = 0; i < len; i++) {
        sum_product += (double)target[i] * (double)ad[i];
        sum_ad_sq += (double)ad[i] * (double)ad[i];
    }
    return sum_product / sum_ad_sq;
}

char* tr_identify(struct sound_seg* target, struct sound_seg* ad) {
    if (!target || !ad || tr_length(ad) > tr_length(target)) return strdup("");
    size_t target_len = tr_length(target);
    size_t ad_len = tr_length(ad);
    int16_t *target_data = (int16_t *)malloc(target_len * sizeof(int16_t));
    int16_t *ad_data = (int16_t *)malloc(ad_len * sizeof(int16_t));
    if (!target_data || !ad_data) {
        free(target_data);
        free(ad_data);
        return strdup("");
    }
    tr_read(target, target_data, 0, target_len);
    tr_read(ad, ad_data, 0, ad_len);

    char *result = (char *)malloc(256);
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
                char *new_result = (char *)realloc(result, capacity);
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

void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track, size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || !src_track->node_count || len == 0 || srcpos + len > tr_length(src_track)) return;

    size_t src_idx = 0;
    size_t src_offset = 0;
    while (src_idx != (size_t)-1 && src_idx < src_track->node_count && src_offset + src_track->nodes[src_idx].length <= srcpos) {
        src_offset += src_track->nodes[src_idx].length;
        src_idx = src_track->nodes[src_idx].next_idx;
    }
    if (src_idx == (size_t)-1 || src_idx >= src_track->node_count) return;

    size_t src_start = srcpos - src_offset;
    if (src_start + len > src_track->nodes[src_idx].length) return;

    src_track->nodes[src_idx].refCount++;

    size_t child_idx = add_node(dest_track, src_track->nodes[src_idx].data, src_track->nodes[src_idx].start + src_start, len, true);
    if (child_idx == (size_t)-1) {
        src_track->nodes[src_idx].refCount--;
        return;
    }

    if (!dest_track->node_count - 1) {
        return; // First node added
    }

    size_t prev_idx = (size_t)-1;
    size_t curr_idx = 0;
    size_t dest_offset = 0;

    while (curr_idx != (size_t)-1 && curr_idx < dest_track->node_count && dest_offset + dest_track->nodes[curr_idx].length <= destpos) {
        dest_offset += dest_track->nodes[curr_idx].length;
        prev_idx = curr_idx;
        curr_idx = dest_track->nodes[curr_idx].next_idx;
    }

    if (curr_idx == (size_t)-1 || curr_idx >= dest_track->node_count) {
        if (prev_idx != (size_t)-1) dest_track->nodes[prev_idx].next_idx = child_idx;
        return;
    }

    size_t split_pos = destpos - dest_offset;
    if (split_pos == 0) {
        dest_track->nodes[child_idx].next_idx = curr_idx;
        if (prev_idx != (size_t)-1) dest_track->nodes[prev_idx].next_idx = child_idx;
    } else {
        size_t right_idx = add_node(dest_track, dest_track->nodes[curr_idx].data, dest_track->nodes[curr_idx].start + split_pos, dest_track->nodes[curr_idx].length - split_pos, dest_track->nodes[curr_idx].is_child);
        if (right_idx == (size_t)-1) return;
        dest_track->nodes[right_idx].refCount = dest_track->nodes[curr_idx].refCount;
        dest_track->nodes[curr_idx].length = split_pos;
        dest_track->nodes[child_idx].next_idx = right_idx;
        dest_track->nodes[right_idx].next_idx = dest_track->nodes[curr_idx].next_idx;
        dest_track->nodes[curr_idx].next_idx = child_idx;
    }
}

int main(int argc, char** argv) {
    struct sound_seg* s0 = tr_init();
    tr_write(s0, ((int16_t[]){}), 0, 0);
    struct sound_seg* s1 = tr_init();
    tr_write(s1, ((int16_t[]){3,18,11,-8,5,-1,-18,-15,0,-6,-5,-14,4}), 0, 13);
    struct sound_seg* s2 = tr_init();
    tr_write(s2, ((int16_t[]){2,19,5,13,-10,-3}), 0, 6);
    struct sound_seg* s3 = tr_init();
    tr_write(s3, ((int16_t[]){-9,-5,20,-12,0,-18,-1,-19,-6}), 0, 9);
    tr_write(s3, ((int16_t[]){11,5,-2,7,-15,8,-13,-1,7}), 0, 9);
    tr_insert(s2, s1, 9, 1, 1);
    tr_write(s2, ((int16_t[]){-20,5,12,0,11,-11}), 0, 6);
    tr_write(s3, ((int16_t[]){-12,-18,-14,-10,5,-9,8,16,-6}), 0, 9);
    tr_delete_range(s3, 5, 3);
    tr_insert(s1, s0, 0, 7, 3);
    tr_write(s1, ((int16_t[]){-10,-6,-7,18,2,-12,12,16,-15,-13,20,-17,17,1}), 0, 14);
    tr_write(s0, ((int16_t[]){17,-16,-11}), 0, 3);
    int16_t failing_read[14];
    tr_read(s1, failing_read, 0, 14);
    for (int i = 0; i < 14; i++) {
        printf("%d ", failing_read[i]);
    }
    printf("\n");
    tr_destroy(s0);
    tr_destroy(s1);
    tr_destroy(s2);
    tr_destroy(s3);
    return 0;
}