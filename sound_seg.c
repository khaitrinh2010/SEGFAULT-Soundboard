#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

//NOTE: Buffer is int16
#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44

// Segment in the linked list
struct segment {
    int16_t* samples;
    size_t length;
    struct segment* next;
    size_t portion_id;
    size_t ref_count;
};

struct sound_seg {
    struct segment* head;
    size_t total_length;
};

#define MAX_SEGMENTS 1000
static struct segment* segment_registry[MAX_SEGMENTS];
static size_t segment_count = 0;
static size_t next_portion_id = 1;

void register_segment(struct segment* seg) {
    if (segment_count < MAX_SEGMENTS) {
        segment_registry[segment_count++] = seg;
    }
}

void unregister_segment(struct segment* seg) {
    for (size_t i = 0; i < segment_count; i++) {
        if (segment_registry[i] == seg) {
            segment_registry[i] = segment_registry[--segment_count];
            break;
        }
    }
}

void wav_load(const char* filename, int16_t* dest){  //wav file header is discarded
    FILE *file;
    file = fopen(filename, "rb");
    if (file == NULL){
      printf("Error opening file\n");
      return;
    }
    //WAV File: RIFF header, FMT sub-chunk, DATA sub-chunk
    fseek(file, OFFSET, SEEK_SET); // Extract how many bytes in the data audio
    uint32_t number_of_bytes;
    fread(&number_of_bytes, sizeof(uint32_t), 1, file); //read how many number_of_bytes excluding the sample I should read
    fseek(file, OFFSET_TO_AUDIO_DATA, SEEK_SET);
    fread(dest, sizeof(int16_t), number_of_bytes / sizeof(int16_t), file); //Read the data and write to the stream
    fclose(file);
    return;
}

void wav_save(const char* fname, int16_t* src, size_t len) {
    FILE *file = fopen(fname, "wb");
    if (file == NULL) {
        printf("Error opening file\n");
        return;
    }

    // Define a union for the WAV header
    union wav_header {
        struct {
            // RIFF chunk
            char riff[4];
            uint32_t flength;
            char wave[4];
            // fmt sub-chunk
            char fmt[4];
            int32_t chunk_size;
            int16_t format_tag;
            int16_t num_chans;
            int32_t sample_rate;
            int32_t bytes_per_sec;
            int16_t bytes_per_sample;
            int16_t bits_per_sample;
            // data sub-chunk
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
    header.fields.format_tag = 1;         // PCM
    header.fields.num_chans = 1;          // Mono
    header.fields.sample_rate = 8000;     // 8000 Hz
    header.fields.bytes_per_sec = 8000 * 2;
    header.fields.bytes_per_sample = 2;
    header.fields.bits_per_sample = 16;
    memcpy(header.fields.data, "data", 4);
    header.fields.dlength = len * sizeof(int16_t);

    fwrite(&header, sizeof(header), 1, file);

    fwrite(src, sizeof(int16_t), len, file);

    fclose(file);
}

// Part 1: Basic sound manipulation
struct sound_seg* tr_init() {
    struct sound_seg* track = malloc(sizeof(struct sound_seg));
    if (!track) return NULL;
    track->head = NULL;
    track->total_length = 0;
    return track;
}

void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    struct segment* current = track->head;
    while (current) {
        struct segment* next = current->next;
        unregister_segment(current);
        free(current->samples); // REQ 1.1
        free(current);
        current = next;
    }
    free(track);
}

size_t tr_length(struct sound_seg* track) {
    return track ? track->total_length : 0;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest || pos >= track->total_length) return;
    size_t remaining = len;
    size_t offset = 0;
    struct segment* current = track->head;

    // Navigate to position
    while (current && pos >= current->length) {
        pos -= current->length;
        current = current->next;
    }

    // Read across segments
    while (current && remaining > 0) {
        size_t copy_len = (pos + remaining > current->length) ? current->length - pos : remaining;
        memcpy(dest + offset, current->samples + pos, copy_len * sizeof(int16_t)); // REQ 2.1
        offset += copy_len;
        remaining -= copy_len;
        pos = 0;
        current = current->next;
    }
}

void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src) return;

    // If empty, create a new segment
    if (!track->head) {
        struct segment* seg = malloc(sizeof(struct segment));
        seg->samples = malloc(len * sizeof(int16_t));
        memcpy(seg->samples, src, len * sizeof(int16_t));
        seg->length = len;
        seg->next = NULL;
        seg->portion_id = next_portion_id++;
        seg->ref_count = 0;
        track->head = seg;
        track->total_length = len;
        register_segment(seg);
        return;
    }

    // Find the segment for pos
    struct segment* current = track->head;
    struct segment* prev = NULL;
    size_t current_pos = 0;

    while (current && current_pos + current->length <= pos) {
        current_pos += current->length;
        prev = current;
        current = current->next;
    }

    // Extend track if needed (REQ 2.2)
    if (current_pos + (current ? current->length : 0) < pos + len) {
        size_t new_total = pos + len;
        if (new_total > track->total_length) {
            track->total_length = new_total;
        }
    }

    // Write logic
    if (current) {
        size_t rel_pos = pos - current_pos;
        if (rel_pos < current->length) {
            // Split segment
            struct segment* new_seg = malloc(sizeof(struct segment));
            new_seg->length = current->length - rel_pos;
            new_seg->samples = malloc(new_seg->length * sizeof(int16_t));
            memcpy(new_seg->samples, current->samples + rel_pos, new_seg->length * sizeof(int16_t));
            new_seg->next = current->next;
            new_seg->portion_id = current->portion_id;
            new_seg->ref_count = 0;
            register_segment(new_seg);

            current->length = rel_pos;
            current->next = new_seg;
        }

        // Insert new segment
        struct segment* write_seg = malloc(sizeof(struct segment));
        write_seg->samples = malloc(len * sizeof(int16_t));
        memcpy(write_seg->samples, src, len * sizeof(int16_t)); // REQ 2.1
        write_seg->length = len;
        write_seg->next = current->next;
        write_seg->portion_id = next_portion_id++;
        write_seg->ref_count = 0;
        current->next = write_seg;
        register_segment(write_seg);

        // Propagate to all segments with the same portion_id (beyond REQ 2.3)
        for (size_t i = 0; i < segment_count; i++) {
            struct segment* seg = segment_registry[i];
            if (seg != write_seg && seg->portion_id == write_seg->portion_id) {
                memcpy(seg->samples, src, len * sizeof(int16_t));
            }
        }
    } else {
        // Append
        struct segment* write_seg = malloc(sizeof(struct segment));
        write_seg->samples = malloc(len * sizeof(int16_t));
        memcpy(write_seg->samples, src, len * sizeof(int16_t));
        write_seg->length = len;
        write_seg->next = NULL;
        write_seg->portion_id = next_portion_id++;
        write_seg->ref_count = 0;
        prev->next = write_seg;
        register_segment(write_seg);

        // Propagate
        for (size_t i = 0; i < segment_count; i++) {
            struct segment* seg = segment_registry[i];
            if (seg != write_seg && seg->portion_id == write_seg->portion_id) {
                memcpy(seg->samples, src, len * sizeof(int16_t));
            }
        }
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || pos >= track->total_length) return false;

    struct segment* current = track->head;
    struct segment* prev = NULL;
    size_t current_pos = 0;
    size_t remaining = len;

    while (current && current_pos + current->length <= pos) {
        current_pos += current->length;
        prev = current;
        current = current->next;
    }

    if (!current) return false;

    // Check ref_count (REQ 3.2)
    struct segment* check = current;
    size_t check_pos = current_pos;
    while (check && check_pos < pos + len) {
        if (check->ref_count > 0) return false;
        check_pos += check->length;
        check = check->next;
    }

    // Delete (REQ 3.1)
    while (current && remaining > 0) {
        size_t rel_pos = pos - current_pos;
        if (rel_pos < current->length) {
            size_t delete_len = (rel_pos + remaining > current->length) ? current->length - rel_pos : remaining;

            if (rel_pos == 0 && delete_len == current->length) {
                if (prev) {
                    prev->next = current->next;
                } else {
                    track->head = current->next;
                }
                unregister_segment(current);
                free(current->samples);
                free(current);
                current = prev ? prev->next : track->head;
            } else if (rel_pos == 0) {
                memmove(current->samples, current->samples + delete_len, (current->length - delete_len) * sizeof(int16_t));
                current->length -= delete_len;
            } else {
                struct segment* new_seg = malloc(sizeof(struct segment));
                new_seg->length = current->length - rel_pos - delete_len;
                new_seg->samples = malloc(new_seg->length * sizeof(int16_t));
                memcpy(new_seg->samples, current->samples + rel_pos + delete_len, new_seg->length * sizeof(int16_t));
                new_seg->next = current->next;
                new_seg->portion_id = current->portion_id;
                new_seg->ref_count = 0;
                register_segment(new_seg);

                current->length = rel_pos;
                current->next = new_seg;
                current = new_seg;
            }
            remaining -= delete_len;
            track->total_length -= delete_len;
        }
        current_pos += current ? current->length : 0;
        prev = current;
        current = current ? current->next : NULL;
    }
    return true;
}

// Part 2: Identify advertisements
char* tr_identify(const struct sound_seg* target, const struct sound_seg* ad) {
    if (!target || !ad || ad->total_length > target->total_length) return strdup("");

    int16_t* target_samples = malloc(target->total_length * sizeof(int16_t));
    int16_t* ad_samples = malloc(ad->total_length * sizeof(int16_t));
    tr_read((struct sound_seg*)target, target_samples, 0, target->total_length);
    tr_read((struct sound_seg*)ad, ad_samples, 0, ad->total_length);

    double auto_corr = 0;
    for (size_t i = 0; i < ad->total_length; i++) {
        auto_corr += (double)ad_samples[i] * ad_samples[i];
    }
    double threshold = 0.95 * auto_corr;

    char* result = malloc(1);
    result[0] = '\0';
    size_t result_len = 1;

    for (size_t i = 0; i <= target->total_length - ad->total_length; i++) {
        double cross_corr = 0;
        for (size_t j = 0; j < ad->total_length; j++) {
            cross_corr += (double)target_samples[i + j] * ad_samples[j];
        }
        if (cross_corr >= threshold) { // REQ 4.1
            char* new_result = malloc(result_len + 20);
            sprintf(new_result, "%s%zu,%zu\n", result, i, i + ad->total_length - 1);
            free(result);
            result = new_result;
            result_len += 20;
            i += ad->total_length - 1; // ASM 4.1: non-overlapping
        }
    }

    if (result_len > 1) result[result_len - 2] = '\0';

    free(target_samples);
    free(ad_samples);
    return result;
}

// Part 3: Complex insertions
void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track,
               size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || srcpos >= src_track->total_length) return;

    // Extract source data
    int16_t* src_data = malloc(len * sizeof(int16_t));
    tr_read(src_track, src_data, srcpos, len);

    // Find insertion point
    struct segment* current = dest_track->head;
    struct segment* prev = NULL;
    size_t current_pos = 0;

    while (current && current_pos + current->length <= destpos) {
        current_pos += current->length;
        prev = current;
        current = current->next;
    }

    // Create new segment (copying, REQ 5.1)
    struct segment* insert_seg = malloc(sizeof(struct segment));
    insert_seg->samples = malloc(len * sizeof(int16_t));
    memcpy(insert_seg->samples, src_data, len * sizeof(int16_t));
    insert_seg->length = len;
    insert_seg->ref_count = 0;

    // Assign portion_id from source
    struct segment* src_current = src_track->head;
    size_t src_current_pos = 0;
    while (src_current && src_current_pos + src_current->length <= srcpos) {
        src_current_pos += src_current->length;
        src_current = src_current->next;
    }
    if (src_current) {
        insert_seg->portion_id = src_current->portion_id;
        src_current->ref_count++;
    } else {
        insert_seg->portion_id = next_portion_id++;
    }
    register_segment(insert_seg);

    free(src_data);

    // Insert into dest_track
    if (!current) {
        if (prev) {
            prev->next = insert_seg;
            insert_seg->next = NULL;
        } else {
            dest_track->head = insert_seg;
            insert_seg->next = NULL;
        }
    } else {
        size_t rel_pos = destpos - current_pos;
        if (rel_pos == 0) {
            insert_seg->next = current;
            if (prev) prev->next = insert_seg;
            else dest_track->head = insert_seg;
        } else {
            struct segment* new_seg = malloc(sizeof(struct segment));
            new_seg->length = current->length - rel_pos;
            new_seg->samples = malloc(new_seg->length * sizeof(int16_t));
            memcpy(new_seg->samples, current->samples + rel_pos, new_seg->length * sizeof(int16_t));
            new_seg->next = current->next;
            new_seg->portion_id = current->portion_id;
            new_seg->ref_count = 0;
            register_segment(new_seg);

            current->length = rel_pos;
            current->next = insert_seg;
            insert_seg->next = new_seg;
        }
    }
    dest_track->total_length += len;
}