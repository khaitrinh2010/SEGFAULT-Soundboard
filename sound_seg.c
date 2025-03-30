#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44

struct sound_seg_node {
    int16_t* samples;        // Contiguous block of samples
    size_t sample_count;     // Number of samples in this block
    struct sound_seg_node* next; // Next node in the list
};

struct sound_seg {
    struct sound_seg_node* head;
};

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
        free(current->samples);
        free(current);
        current = next;
    }
    free(track);
}

// Get track length (total samples)
size_t tr_length(struct sound_seg* track) {
    if (!track) return 0;
    size_t length = 0;
    struct sound_seg_node* current = track->head;
    while (current) {
        length += current->sample_count;
        current = current->next;
    }
    return length;
}

// Read samples from track
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest || len == 0 || pos >= tr_length(track)) return;
    struct sound_seg_node* current = track->head;
    size_t current_pos = 0;

    // Skip to the starting position
    while (current && current_pos + current->sample_count <= pos) {
        current_pos += current->sample_count;
        current = current->next;
    }
    if (!current) return;

    size_t dest_offset = 0;
    size_t remaining = len;

    // Read from the first node
    if (current_pos < pos) {
        size_t offset_in_node = pos - current_pos;
        size_t to_copy = current->sample_count - offset_in_node;
        if (to_copy > remaining) to_copy = remaining;
        memcpy(dest, current->samples + offset_in_node, to_copy * sizeof(int16_t));
        dest_offset += to_copy;
        remaining -= to_copy;
        current = current->next;
    }

    // Read from subsequent nodes
    while (current && remaining > 0) {
        size_t to_copy = current->sample_count;
        if (to_copy > remaining) to_copy = remaining;
        memcpy(dest + dest_offset, current->samples, to_copy * sizeof(int16_t));
        dest_offset += to_copy;
        remaining -= to_copy;
        current = current->next;
    }
}

// Write samples to track
void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;

    struct sound_seg_node* current = track->head;
    struct sound_seg_node* prev = NULL;
    size_t current_pos = 0;

    // Find the node containing 'pos' or where it should be inserted
    while (current && current_pos + current->sample_count <= pos) {
        current_pos += current->sample_count;
        prev = current;
        current = current->next;
    }

    // Fill gap with a new node if pos is beyond current length
    if (!current && pos > current_pos) {
        struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
        if (!new_node) return;
        new_node->sample_count = pos - current_pos;
        new_node->samples = calloc(new_node->sample_count, sizeof(int16_t)); // Zero-fill gap
        if (!new_node->samples) { free(new_node); return; }
        new_node->next = NULL;
        if (prev) prev->next = new_node;
        else track->head = new_node;
        prev = new_node;
        current_pos = pos;
    }

    // If pos is within an existing node, split it
    if (current && current_pos < pos) {
        size_t offset = pos - current_pos;
        if (offset > 0) {
            struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
            if (!new_node) return;
            new_node->sample_count = offset;
            new_node->samples = malloc(offset * sizeof(int16_t));
            if (!new_node->samples) { free(new_node); return; }
            memcpy(new_node->samples, current->samples, offset * sizeof(int16_t));
            new_node->next = current;
            if (prev) prev->next = new_node;
            else track->head = new_node;
            prev = new_node;

            // Adjust the current node
            int16_t* new_samples = malloc((current->sample_count - offset) * sizeof(int16_t));
            if (!new_samples) return;
            memcpy(new_samples, current->samples + offset, (current->sample_count - offset) * sizeof(int16_t));
            free(current->samples);
            current->samples = new_samples;
            current->sample_count -= offset;
            current_pos = pos;
        }
    }

    // Write the data
    size_t remaining = len;
    size_t src_offset = 0;

    while (remaining > 0) {
        if (current && current_pos == pos + src_offset) {
            // Overwrite existing node or part of it
            size_t to_write = remaining < current->sample_count ? remaining : current->sample_count;
            memcpy(current->samples, src + src_offset, to_write * sizeof(int16_t));
            src_offset += to_write;
            remaining -= to_write;
            current_pos += to_write;

            // If we didn't use the whole node, split it
            if (to_write < current->sample_count) {
                struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
                if (!new_node) return;
                new_node->sample_count = current->sample_count - to_write;
                new_node->samples = malloc(new_node->sample_count * sizeof(int16_t));
                if (!new_node->samples) { free(new_node); return; }
                memcpy(new_node->samples, current->samples + to_write, new_node->sample_count * sizeof(int16_t));
                new_node->next = current->next;
                current->next = new_node;
                free(current->samples);
                current->samples = malloc(to_write * sizeof(int16_t));
                if (!current->samples) return;
                memcpy(current->samples, src + src_offset - to_write, to_write * sizeof(int16_t));
                current->sample_count = to_write;
            }
            prev = current;
            current = current->next;
        } else {
            // Insert a new node
            struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
            if (!new_node) return;
            new_node->sample_count = remaining;
            new_node->samples = malloc(new_node->sample_count * sizeof(int16_t));
            if (!new_node->samples) { free(new_node); return; }
            memcpy(new_node->samples, src + src_offset, remaining * sizeof(int16_t));
            new_node->next = current;
            if (prev) prev->next = new_node;
            else track->head = new_node;
            src_offset += remaining;
            remaining = 0;
        }
    }
}

// Delete a range of samples from track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || len == 0 || pos >= tr_length(track)) return false;
    struct sound_seg_node* current = track->head;
    struct sound_seg_node* prev = NULL;
    size_t current_pos = 0;

    while (current && current_pos + current->sample_count <= pos) {
        current_pos += current->sample_count;
        prev = current;
        current = current->next;
    }
    if (!current) return false;

    size_t remaining = len;

    // Handle partial deletion at the start
    if (current_pos < pos) {
        size_t offset = pos - current_pos;
        size_t new_count = offset;
        int16_t* new_samples = malloc(new_count * sizeof(int16_t));
        if (!new_samples) return false;
        memcpy(new_samples, current->samples, new_count * sizeof(int16_t));
        free(current->samples);
        current->samples = new_samples;
        current->sample_count = new_count;
        prev = current;
        current = current->next;
        remaining -= (pos - current_pos);
        current_pos = pos;
    }

    // Delete full nodes or parts of nodes
    while (current && remaining > 0) {
        if (current->sample_count <= remaining) {
            // Delete entire node
            struct sound_seg_node* next = current->next;
            free(current->samples);
            free(current);
            if (prev) prev->next = next;
            else track->head = next;
            remaining -= current->sample_count;
            current_pos += current->sample_count;
            current = next;
        } else {
            // Partial deletion
            size_t to_keep = current->sample_count - remaining;
            int16_t* new_samples = malloc(to_keep * sizeof(int16_t));
            if (!new_samples) return false;
            memcpy(new_samples, current->samples + remaining, to_keep * sizeof(int16_t));
            free(current->samples);
            current->samples = new_samples;
            current->sample_count = to_keep;
            remaining = 0;
        }
    }
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
    if (!src_track || !dest_track || len == 0 || srcpos >= tr_length(src_track) || srcpos + len > tr_length(src_track)) return;

    // Read the source segment into a temporary buffer
    int16_t* temp = malloc(len * sizeof(int16_t));
    if (!temp) return;
    tr_read(src_track, temp, srcpos, len);

    // Find the insertion point in dest_track
    struct sound_seg_node* current = dest_track->head;
    struct sound_seg_node* prev = NULL;
    size_t current_pos = 0;

    while (current && current_pos + current->sample_count <= destpos) {
        current_pos += current->sample_count;
        prev = current;
        current = current->next;
    }

    // If destpos is beyond current length, fill gap
    if (!current && destpos > current_pos) {
        struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
        if (!new_node) { free(temp); return; }
        new_node->sample_count = destpos - current_pos;
        new_node->samples = calloc(new_node->sample_count, sizeof(int16_t));
        if (!new_node->samples) { free(new_node); free(temp); return; }
        new_node->next = NULL;
        if (prev) prev->next = new_node;
        else dest_track->head = new_node;
        prev = new_node;
        current_pos = destpos;
    }

    // Split node if destpos is within it
    if (current && current_pos < destpos) {
        size_t offset = destpos - current_pos;
        struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
        if (!new_node) { free(temp); return; }
        new_node->sample_count = offset;
        new_node->samples = malloc(offset * sizeof(int16_t));
        if (!new_node->samples) { free(new_node); free(temp); return; }
        memcpy(new_node->samples, current->samples, offset * sizeof(int16_t));
        new_node->next = current;
        if (prev) prev->next = new_node;
        else dest_track->head = new_node;

        int16_t* new_samples = malloc((current->sample_count - offset) * sizeof(int16_t));
        if (!new_samples) { free(temp); return; }
        memcpy(new_samples, current->samples + offset, (current->sample_count - offset) * sizeof(int16_t));
        free(current->samples);
        current->samples = new_samples;
        current->sample_count -= offset;
        prev = new_node;
    }

    // Insert the new node
    struct sound_seg_node* insert_node = malloc(sizeof(struct sound_seg_node));
    if (!insert_node) { free(temp); return; }
    insert_node->sample_count = len;
    insert_node->samples = temp; // Use the allocated temp buffer
    insert_node->next = current;
    if (prev) prev->next = insert_node;
    else dest_track->head = insert_node;
}

int main(int argc, char** argv) {
}