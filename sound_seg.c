#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Structure for tracking shared references
struct ref {
    size_t start;           // Start index in this track's data
    size_t len;             // Length of the shared segment
    struct sound_seg* parent; // Parent track (NULL if not shared)
    size_t ref_count;       // Number of children referencing this segment
};

// Track structure
struct sound_seg {
    int16_t* data;          // Contiguous array of samples
    size_t total_len;       // Total length of samples
    size_t capacity;        // Allocated size of data array
    struct ref* refs;       // Array of shared reference metadata
    size_t ref_count;       // Number of refs
    size_t ref_capacity;    // Allocated size of refs array
};

// WAV file interaction (simplified)
void wav_load(const char* fname, int16_t* dest) {
    FILE* fp = fopen(fname, "rb");
    if (!fp) return;
    fseek(fp, 44, SEEK_SET);
    fread(dest, sizeof(int16_t), 8000, fp);
    fclose(fp);
}

void wav_save(const char* fname, const int16_t* src, size_t len) {
    FILE* fp = fopen(fname, "wb");
    if (!fp) return;
    uint32_t sample_rate = 8000;
    uint32_t byte_rate = sample_rate * 2;
    uint32_t data_size = len * 2;
    fwrite("RIFF", 1, 4, fp);
    uint32_t chunk_size = 36 + data_size;
    fwrite(&chunk_size, 4, 1, fp);
    fwrite("WAVEfmt ", 1, 8, fp);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, fp);
    uint16_t audio_format = 1;
    fwrite(&audio_format, 2, 1, fp);
    uint16_t num_channels = 1;
    fwrite(&num_channels, 2, 1, fp);
    fwrite(&sample_rate, 4, 1, fp);
    fwrite(&byte_rate, 4, 1, fp);
    uint16_t block_align = 2;
    fwrite(&block_align, 2, 1, fp);
    uint16_t bits_per_sample = 16;
    fwrite(&bits_per_sample, 2, 1, fp);
    fwrite("data", 1, 4, fp);
    fwrite(&data_size, 4, 1, fp);
    fwrite(src, sizeof(int16_t), len, fp);
    fclose(fp);
}

struct sound_seg* tr_init(void) {
    struct sound_seg* track = malloc(sizeof(struct sound_seg));
    if (!track) return NULL;
    track->data = NULL;
    track->total_len = 0;
    track->capacity = 0;
    track->refs = NULL;
    track->ref_count = 0;
    track->ref_capacity = 0;
    return track;
}

void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    // Only free data if no children reference it
    bool can_free_data = true;
    for (size_t i = 0; i < track->ref_count; i++) {
        if (track->refs[i].parent == NULL && track->refs[i].ref_count > 0) {
            can_free_data = false;
            break;
        }
    }
    if (can_free_data) free(track->data);
    free(track->refs);
    free(track);
}

size_t tr_length(struct sound_seg* track) {
    return track ? track->total_len : 0;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || pos >= track->total_len) return;
    size_t end = pos + len > track->total_len ? track->total_len : pos + len;
    memcpy(dest, track->data + pos, (end - pos) * sizeof(int16_t));
}

void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src) return;
    size_t new_len = pos + len > track->total_len ? pos + len : track->total_len;

    // Resize data array if necessary
    if (new_len > track->capacity) {
        size_t new_capacity = track->capacity ? track->capacity * 2 : 16;
        while (new_capacity < new_len) new_capacity *= 2;
        int16_t* new_data = realloc(track->data, new_capacity * sizeof(int16_t));
        if (!new_data) return; // Memory allocation failure
        track->data = new_data;
        track->capacity = new_capacity;
    }

    // Copy new data
    memcpy(track->data + pos, src, len * sizeof(int16_t));
    track->total_len = new_len;

    // Update references (if any overlap, they remain valid as data pointer is shared)
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || pos + len > track->total_len) return false;

    // Check if any segment in range is a parent with children
    for (size_t i = 0; i < track->ref_count; i++) {
        if (track->refs[i].parent == NULL) { // This is a parent
            size_t ref_end = track->refs[i].start + track->refs[i].len;
            if (track->refs[i].ref_count > 0 &&
                pos < ref_end && pos + len > track->refs[i].start) {
                return false; // Cannot delete a parent with children
            }
        }
    }

    // Shift data after the deleted range
    size_t remaining = track->total_len - (pos + len);
    if (remaining > 0) {
        memmove(track->data + pos, track->data + pos + len, remaining * sizeof(int16_t));
    }
    track->total_len -= len;

    // Update references
    for (size_t i = 0; i < track->ref_count; i++) {
        size_t ref_end = track->refs[i].start + track->refs[i].len;
        if (pos < ref_end && pos + len > track->refs[i].start) {
            // Overlaps with deleted range
            if (pos <= track->refs[i].start && pos + len >= ref_end) {
                // Entire reference deleted
                if (track->refs[i].parent) track->refs[i].parent->refs[0].ref_count--; // Assuming single ref in parent
                memmove(&track->refs[i], &track->refs[i + 1], (track->ref_count - i - 1) * sizeof(struct ref));
                track->ref_count--;
                i--;
            } else if (pos > track->refs[i].start) {
                track->refs[i].len = pos - track->refs[i].start;
            } else {
                size_t shift = pos + len - track->refs[i].start;
                track->refs[i].start = pos;
                track->refs[i].len -= shift;
            }
        } else if (track->refs[i].start >= pos + len) {
            track->refs[i].start -= len;
        }
    }
    return true;
}

char* tr_identify(const struct sound_seg* target, const struct sound_seg* ad) {
    size_t target_len = tr_length((struct sound_seg*)target);
    size_t ad_len = tr_length((struct sound_seg*)ad);
    if (target_len < ad_len) return strdup("");

    int16_t* target_data = malloc(target_len * sizeof(int16_t));
    int16_t* ad_data = malloc(ad_len * sizeof(int16_t));
    tr_read((struct sound_seg*)target, target_data, 0, target_len);
    tr_read((struct sound_seg*)ad, ad_data, 0, ad_len);

    double auto_corr = 0;
    for (size_t i = 0; i < ad_len; i++) {
        auto_corr += (double)ad_data[i] * ad_data[i];
    }

    char* result = malloc(1);
    result[0] = '\0';
    size_t result_len = 1;

    for (size_t i = 0; i <= target_len - ad_len; i++) {
        double cross_corr = 0;
        for (size_t j = 0; j < ad_len; j++) {
            cross_corr += (double)target_data[i + j] * ad_data[j];
        }
        if (cross_corr >= 0.95 * auto_corr) {
            char buffer[32];
            snprintf(buffer, 32, "%zu,%zu\n", i, i + ad_len - 1);
            size_t new_len = strlen(buffer);
            result = realloc(result, result_len + new_len);
            strcpy(result + result_len - 1, buffer);
            result_len += new_len - 1;
        }
    }

    free(target_data);
    free(ad_data);
    if (result_len > 1) result[result_len - 1] = '\0';
    return result;
}

void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track,
              size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || srcpos + len > src_track->total_len) return;

    // Resize dest_track if necessary
    size_t new_len = destpos + len + (dest_track->total_len > destpos ? dest_track->total_len - destpos : 0);
    if (new_len > dest_track->capacity) {
        size_t new_capacity = dest_track->capacity ? dest_track->capacity * 2 : 16;
        while (new_capacity < new_len) new_capacity *= 2;
        int16_t* new_data = realloc(dest_track->data, new_capacity * sizeof(int16_t));
        if (!new_data) return;
        dest_track->data = new_data;
        dest_track->capacity = new_capacity;
    }

    // Shift existing data to make room
    if (destpos < dest_track->total_len) {
        memmove(dest_track->data + destpos + len, dest_track->data + destpos,
                (dest_track->total_len - destpos) * sizeof(int16_t));
    }

    // Copy data from source (shared reference)
    memcpy(dest_track->data + destpos, src_track->data + srcpos, len * sizeof(int16_t));
    dest_track->total_len = new_len;

    // Add reference to dest_track
    if (dest_track->ref_count >= dest_track->ref_capacity) {
        dest_track->ref_capacity = dest_track->ref_capacity ? dest_track->ref_capacity * 2 : 4;
        struct ref* new_refs = realloc(dest_track->refs, dest_track->ref_capacity * sizeof(struct ref));
        if (!new_refs) return;
        dest_track->refs = new_refs;
    }
    dest_track->refs[dest_track->ref_count].start = destpos;
    dest_track->refs[dest_track->ref_count].len = len;
    dest_track->refs[dest_track->ref_count].parent = src_track;
    dest_track->refs[dest_track->ref_count].ref_count = 0;
    dest_track->ref_count++;

    // Add parent reference to src_track if not already present
    bool found = false;
    for (size_t i = 0; i < src_track->ref_count; i++) {
        if (src_track->refs[i].start == srcpos && src_track->refs[i].len == len && src_track->refs[i].parent == NULL) {
            src_track->refs[i].ref_count++;
            found = true;
            break;
        }
    }
    if (!found) {
        if (src_track->ref_count >= src_track->ref_capacity) {
            src_track->ref_capacity = src_track->ref_capacity ? src_track->ref_capacity * 2 : 4;
            struct ref* new_refs = realloc(src_track->refs, src_track->ref_capacity * sizeof(struct ref));
            if (!new_refs) return;
            src_track->refs = new_refs;
        }
        src_track->refs[src_track->ref_count].start = srcpos;
        src_track->refs[src_track->ref_count].len = len;
        src_track->refs[src_track->ref_count].parent = NULL;
        src_track->refs[src_track->ref_count].ref_count = 1;
        src_track->ref_count++;
    }
}

int main (void) {
    return 0;
}