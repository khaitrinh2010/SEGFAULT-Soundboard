#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44

struct sound_seg_node {
    int16_t sample;              // Audio sample value
    uint16_t id;                 // Unique identifier
    struct sound_seg_node** children; // Array of child nodes
    uint8_t child_count;         // Number of children (max 255)
    uint8_t ref_count;           // Reference count (parents + self)
};

struct sound_seg {
    struct sound_seg_node** nodes; // Array of nodes in track order
    size_t node_count;           // Number of nodes
};

static uint16_t next_id = 1;

// Propagate sample update to all children recursively
static void update_children(struct sound_seg_node* node, int16_t new_sample) {
    if (!node) return;
    node->sample = new_sample;
    for (uint8_t i = 0; i < node->child_count; i++) {
        if (node->children[i]) update_children(node->children[i], new_sample);
    }
}

// WAV file interaction
void wav_load(const char* filename, int16_t* dest) {
    FILE *file = fopen(filename, "rb");
    if (!file) return;
    fseek(file, OFFSET, SEEK_SET);
    uint32_t number_of_bytes;
    fread(&number_of_bytes, sizeof(uint32_t), 1, file);
    fseek(file, OFFSET_TO_AUDIO_DATA, SEEK_SET);
    fread(dest, sizeof(int16_t), number_of_bytes / sizeof(int16_t), file);
    fclose(file);
}

void wav_save(const char* fname, const int16_t* src, size_t len) {
    FILE *file = fopen(fname, "wb");
    if (!file) return;
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
    } header = {
        .riff = "RIFF",
        .flength = len * sizeof(int16_t) + OFFSET_TO_AUDIO_DATA - 8,
        .wave = "WAVE",
        .fmt = "fmt ",
        .chunk_size = 16,
        .format_tag = 1,
        .num_chans = 1,
        .sample_rate = 8000,
        .bytes_per_sec = 8000 * 2,
        .bytes_per_sample = 2,
        .bits_per_sample = 16,
        .data = "data",
        .dlength = len * sizeof(int16_t)
    };
    fwrite(&header, sizeof(header), 1, file);
    fwrite(src, sizeof(int16_t), len, file);
    fclose(file);
}

// Track management
struct sound_seg* tr_init(void) {
    struct sound_seg* track = malloc(sizeof(struct sound_seg));
    if (!track) return NULL;
    track->nodes = NULL;
    track->node_count = 0;
    return track;
}

void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    for (size_t i = 0; i < track->node_count; i++) {
        if (track->nodes[i]) {
            free(track->nodes[i]->children);
            free(track->nodes[i]);
        }
    }
    free(track->nodes);
    free(track);
}

size_t tr_length(struct sound_seg* track) {
    return track ? track->node_count : 0;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest || pos >= track->node_count) return;
    size_t end = pos + len > track->node_count ? track->node_count : pos + len;
    for (size_t i = pos; i < end; i++) {
        dest[i - pos] = track->nodes[i]->sample;
    }
}

void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src) return;
    size_t new_size = pos + len > track->node_count ? pos + len : track->node_count;
    if (new_size > track->node_count) {
        struct sound_seg_node** new_nodes = realloc(track->nodes, new_size * sizeof(struct sound_seg_node*));
        if (!new_nodes) return;
        track->nodes = new_nodes;
        for (size_t i = track->node_count; i < new_size; i++) {
            track->nodes[i] = malloc(sizeof(struct sound_seg_node));
            if (!track->nodes[i]) return;
            track->nodes[i]->sample = 0;
            track->nodes[i]->id = next_id++;
            track->nodes[i]->children = NULL;
            track->nodes[i]->child_count = 0;
            track->nodes[i]->ref_count = 1; // Self-reference
        }
        track->node_count = new_size;
    }
    for (size_t i = 0; i < len; i++) {
        track->nodes[pos + i]->sample = src[i];
        update_children(track->nodes[pos + i], src[i]);
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || pos >= track->node_count) return false;
    size_t end = pos + len > track->node_count ? track->node_count : pos + len;

    for (size_t i = pos; i < end; i++) {
        if (track->nodes[i]->child_count > 0) return false; // REQ 3.2: Can't delete if it has children
    }

    for (size_t i = pos; i < end; i++) {
        if (--track->nodes[i]->ref_count == 0) {
            free(track->nodes[i]->children);
            free(track->nodes[i]);
        }
        track->nodes[i] = NULL; // Mark as deleted
    }

    size_t remaining = track->node_count - end;
    if (pos + remaining < track->node_count) {
        memmove(track->nodes + pos, track->nodes + end, remaining * sizeof(struct sound_seg_node*));
    }
    track->node_count -= (end - pos);
    struct sound_seg_node** new_nodes = realloc(track->nodes, track->node_count * sizeof(struct sound_seg_node*));
    if (new_nodes || track->node_count == 0) track->nodes = new_nodes;
    return true;
}

double compute_cross_correlation(const int16_t* target, const int16_t* ad, size_t len) {
    double sum_product = 0.0;
    double sum_ad_sq = 0.0;
    for (size_t i = 0; i < len; i++) {
        sum_product += (double)target[i] * (double)ad[i];
        sum_ad_sq += (double)ad[i] * (double)ad[i];
    }
    return sum_ad_sq ? sum_product / sum_ad_sq : 0.0;
}

char* tr_identify(const struct sound_seg* target, const struct sound_seg* ad) {
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
    tr_read((struct sound_seg*)target, target_data, 0, target_len);
    tr_read((struct sound_seg*)ad, ad_data, 0, ad_len);

    char* result = malloc(256);
    if (!result) {
        free(target_data);
        free(ad_data);
        return strdup("");
    }
    size_t capacity = 256, used = 0;
    result[0] = '\0';

    double ad_auto = compute_cross_correlation(ad_data, ad_data, ad_len);
    if (ad_auto == 0.0) {
        free(target_data);
        free(ad_data);
        free(result);
        return strdup("");
    }

    for (size_t i = 0; i <= target_len - ad_len; i++) {
        double corr = compute_cross_correlation(target_data + i, ad_data, ad_len);
        if (corr / ad_auto >= 0.95) {
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
            i += ad_len - 1; // Non-overlapping (ASM 4.1)
        }
    }
    free(target_data);
    free(ad_data);
    if (used > 0 && result[used - 1] == '\n') result[used - 1] = '\0';
    return result;
}

void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track, size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || srcpos + len > src_track->node_count) return;

    size_t new_size = destpos + len > dest_track->node_count ? destpos + len : dest_track->node_count;
    if (new_size > dest_track->node_count) {
        struct sound_seg_node** new_nodes = realloc(dest_track->nodes, new_size * sizeof(struct sound_seg_node*));
        if (!new_nodes) return;
        dest_track->nodes = new_nodes;
        for (size_t i = dest_track->node_count; i < new_size; i++) {
            dest_track->nodes[i] = malloc(sizeof(struct sound_seg_node));
            if (!dest_track->nodes[i]) return;
            dest_track->nodes[i]->sample = 0;
            dest_track->nodes[i]->id = next_id++;
            dest_track->nodes[i]->children = NULL;
            dest_track->nodes[i]->child_count = 0;
            dest_track->nodes[i]->ref_count = 1;
        }
        dest_track->node_count = new_size;
    }

    for (size_t i = 0; i < len; i++) {
        struct sound_seg_node* src_node = src_track->nodes[srcpos + i];
        struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
        if (!new_node) return;
        new_node->sample = src_node->sample;
        new_node->id = next_id++;
        new_node->children = NULL;
        new_node->child_count = 0;
        new_node->ref_count = 1;

        src_node->children = realloc(src_node->children, (src_node->child_count + 1) * sizeof(struct sound_seg_node*));
        if (!src_node->children) { free(new_node); return; }
        src_node->children[src_node->child_count++] = new_node;
        src_node->ref_count++;

        if (dest_track->nodes[destpos + i]) {
            if (--dest_track->nodes[destpos + i]->ref_count == 0) {
                free(dest_track->nodes[destpos + i]->children);
                free(dest_track->nodes[destpos + i]);
            }
        }
        dest_track->nodes[destpos + i] = new_node;
    }
}