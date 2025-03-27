#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

//NOTE: Buffer is int16
#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44

struct portion {
    int16_t* data;              // Pointer to audio samples (shared or owned)
    size_t len;                 // Length of this portion in samples
    bool owned;                 // True if this portion owns the data (frees it)
    struct portion* parent;     // Pointer to parent portion (NULL if no parent)
    size_t child_count;         // Number of children referencing this portion
    struct portion* next;       // Next portion in the track
};

// Structure for a track
struct sound_seg {
    struct portion* head;       // Head of the portion list
    size_t total_len;           // Total length of the track in samples
};

struct wav_header {
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
 };

// void print_track_metadata(struct sound_seg* track, const char* track_name) {
//     printf("{\n");
//     printf("  \"track_name\": \"%s\",\n", track_name);
//     printf("  \"total_number_of_segments\": %zu,\n", track->total_number_of_segments);
//     printf("  \"segments\": [\n");
//
//     struct sound_seg_node* node = track->head;
//     int index = 0;
//     while (node) {
//         printf("    {\n");
//         printf("      \"segment_index\": %d,\n", index);
//         printf("      \"length_of_the_segment\": %zu,\n", node->length_of_the_segment);
//         printf("      \"owns_data\": %s,\n", node->owns_data ? "true" : "false");
//         printf("      \"ref_count\": %d,\n", node->ref_count);
//         printf("      \"address of audio data\": %p,\n", node->audio_data);
//         printf("      \"audio_data\": [");
//         for (size_t i = 0; i < node->length_of_the_segment; i++) {
//             printf("%d", node->audio_data[i]);
//             if (i != node->length_of_the_segment - 1) printf(", ");
//         }
//         printf("],\n");
//         if (node->parent_node != NULL) {
//             printf("      \"parent\": {\n");
//             printf("        \"length_of_the_segment\": %zu,\n", node->parent_node->length_of_the_segment);
//             printf("        \"ref_count\": %d\n", node->parent_node->ref_count);
//             printf("      }\n");
//         } else {
//             printf("      \"parent\": null\n");
//         }
//
//         printf("    }");
//         node = node->next;
//         if (node) printf(",");
//         printf("\n");
//         index++;
//     }
//     printf("  ]\n");
//     printf("}\n");
// }


// Load a WAV file into buffer
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

// Create/write a WAV file from buffer
void wav_save(const char* fname, int16_t* src, size_t len){
    // The songs will always be PCM, 16 bits per sample, mono, 8000Hz Sample rate
    FILE *file;
    file = fopen(fname, "wb");
    if (file == NULL){
      printf("Error opening file\n");
      return;
    }
    //WRITE HEADER FIRST
    struct wav_header header;
    strncpy(header.riff, "RIFF", 4);
    strncpy(header.wave, "WAVE", 4);
    strncpy(header.fmt, "fmt ", 4);
    strncpy(header.data, "data", 4);

    header.chunk_size = 16;
    header.format_tag = 1;
    header.num_chans = 1;
    header.sample_rate = 8000;
    header.bits_per_sample = 16;
    header.bytes_per_sample = header.bits_per_sample / 8;
    header.bytes_per_sec = header.bytes_per_sample * header.sample_rate;
    // len is the number of samples
    header.dlength = len * sizeof(int16_t);
    header.flength = header.dlength + OFFSET_TO_AUDIO_DATA;
    fwrite(&header, sizeof(struct wav_header), 1, file);
    fseek(file, OFFSET_TO_AUDIO_DATA, SEEK_SET);
    fwrite(src, sizeof(int16_t), len, file);
    fclose(file);
    return;
}

// Initialize a new sound_seg object
// Track management functions
struct sound_seg* tr_init() {
    struct sound_seg* track = malloc(sizeof(struct sound_seg));
    if (!track) return NULL;
    track->head = NULL;
    track->total_len = 0;
    return track;
}

void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    struct portion* current = track->head;
    while (current) {
        struct portion* next = current->next;
        // Only free data if this portion owns it and has no children
        if (current->owned && current->child_count == 0 && current->data) {
            free(current->data);
        }
        free(current);
        current = next;
    }
    free(track);
}

size_t tr_length(struct sound_seg* track) {
    return track ? track->total_len : 0;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !track->head || pos >= track->total_len) return;
    size_t copied = 0;
    struct portion* current = track->head;
    size_t offset = 0;

    // Skip to starting position
    while (current && offset + current->len <= pos) {
        offset += current->len;
        current = current->next;
    }

    // Copy data from portions
    while (current && copied < len) {
        size_t portion_pos = pos - offset;
        size_t to_copy = current->len - portion_pos;
        if (to_copy > len - copied) to_copy = len - copied;
        memcpy(dest + copied, current->data + portion_pos, to_copy * sizeof(int16_t));
        copied += to_copy;
        offset += current->len;
        current = current->next;
    }
}
void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src) return;

    if (!track->head) {
        struct portion* new_portion = malloc(sizeof(struct portion));
        if (!new_portion) return;
        new_portion->data = malloc(len * sizeof(int16_t));
        if (!new_portion->data) { free(new_portion); return; }
        memcpy(new_portion->data, src, len * sizeof(int16_t));
        new_portion->len = len;
        new_portion->owned = true;
        new_portion->parent = NULL;
        new_portion->child_count = 0;
        new_portion->next = NULL;
        track->head = new_portion;
        track->total_len = len;
        return;
    }

    struct portion* current = track->head;
    struct portion* prev = NULL;
    size_t offset = 0;
    size_t remaining = len;
    size_t src_offset = 0;

    while (current && offset + current->len <= pos) {
        offset += current->len;
        prev = current;
        current = current->next;
    }

    while (remaining > 0 && (current || offset == pos)) {
        if (!current) {
            struct portion* new_portion = malloc(sizeof(struct portion));
            if (!new_portion) return;
            new_portion->data = malloc(remaining * sizeof(int16_t));
            if (!new_portion->data) { free(new_portion); return; }
            memcpy(new_portion->data, src + src_offset, remaining * sizeof(int16_t));
            new_portion->len = remaining;
            new_portion->owned = true;
            new_portion->parent = NULL;
            new_portion->child_count = 0;
            new_portion->next = NULL;
            if (prev) prev->next = new_portion;
            else track->head = new_portion;
            track->total_len = pos + len;
            return;
        }

        size_t portion_start = offset;
        size_t portion_end = offset + current->len;
        size_t write_start = (pos > portion_start) ? pos - portion_start : 0;
        size_t write_len = (pos + len < portion_end) ? len : portion_end - pos;
        if (write_len > remaining) write_len = remaining;

        // Resize to cover full write if necessary
        if (write_start + remaining > current->len || (!current->owned && current->parent)) {
            size_t new_size = write_start + remaining > current->len ? pos + len - offset : current->len;
            int16_t* new_data = malloc(new_size * sizeof(int16_t));
            if (!new_data) return;
            memcpy(new_data, current->data, current->len * sizeof(int16_t));
            if (current->owned && current->child_count == 0) free(current->data);
            else if (current->parent) current->parent->child_count--;
            current->data = new_data;
            current->len = new_size;
            current->owned = true;
            current->parent = NULL;
        }

        memcpy(current->data + write_start, src + src_offset, write_len * sizeof(int16_t));

        remaining -= write_len;
        src_offset += write_len;
        offset += current->len;
        prev = current;
        current = current->next;

        if (pos + src_offset < portion_end) {
            struct portion* after = malloc(sizeof(struct portion));
            if (!after) return;
            after->data = prev->data + (pos + src_offset - portion_start);
            after->len = portion_end - (pos + src_offset);
            after->owned = false;
            after->parent = prev;
            after->child_count = 0;
            after->next = current;
            prev->len = pos + src_offset - portion_start;
            prev->next = after;
            prev->child_count++;
            break;
        }
    }

    if (remaining > 0) {
        struct portion* new_portion = malloc(sizeof(struct portion));
        if (!new_portion) return;
        new_portion->data = malloc(remaining * sizeof(int16_t));
        if (!new_portion->data) { free(new_portion); return; }
        memcpy(new_portion->data, src + src_offset, remaining * sizeof(int16_t));
        new_portion->len = remaining;
        new_portion->owned = true;
        new_portion->parent = NULL;
        new_portion->child_count = 0;
        new_portion->next = NULL;
        prev->next = new_portion;
        track->total_len = pos + len;
    } else if (pos + len > track->total_len) {
        track->total_len = pos + len;
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || pos + len > track->total_len) return false;
    if (len == 0) return true;

    struct portion* current = track->head;
    struct portion* prev = NULL;
    size_t offset = 0;

    while (current && offset + current->len <= pos) {
        offset += current->len;
        prev = current;
        current = current->next;
    }

    if (!current) return false;

    struct portion* check = current;
    size_t check_offset = offset;
    while (check && check_offset < pos + len) {
        if (check->child_count > 0) {
            size_t start = (check_offset < pos) ? pos - check_offset : 0;
            size_t end = (check_offset + check->len > pos + len) ? pos + len - check_offset : check->len;
            if (start < end) return false;
        }
        check_offset += check->len;
        check = check->next;
    }

    while (current && offset < pos + len) {
        size_t portion_start = offset;
        size_t portion_end = offset + current->len;

        if (pos > portion_start && pos + len < portion_end) {
            struct portion* after = malloc(sizeof(struct portion));
            if (!after) return false;
            after->len = portion_end - (pos + len);
            after->data = malloc(after->len * sizeof(int16_t));
            memcpy(after->data, current->data + (pos + len - portion_start), after->len * sizeof(int16_t));
            after->owned = true;
            after->parent = NULL;
            after->child_count = 0;
            after->next = current->next;

            current->len = pos - portion_start;
            current->next = after;
            track->total_len -= len;
            break;
        } else if (pos > portion_start) {
            current->len = pos - portion_start;
            prev = current;
            current = current->next;
            offset = portion_end;
        } else if (pos + len < portion_end) {
            size_t new_len = portion_end - (pos + len);
            memmove(current->data, current->data + len, new_len * sizeof(int16_t));
            current->len = new_len;
            track->total_len -= len;
            break;
        } else {
            struct portion* to_free = current;
            current = current->next;
            if (prev) prev->next = current;
            else track->head = current;
            if (to_free->owned && to_free->child_count == 0) free(to_free->data);
            free(to_free);
            track->total_len -= to_free->len;
            offset = portion_end;
        }
    }
    while (current && offset < pos + len) {
        struct portion* to_free = current;
        current = current->next;
        if (prev) prev->next = current;
        else track->head = current;
        track->total_len -= to_free->len;
        if (to_free->owned && to_free->child_count == 0) free(to_free->data);
        free(to_free);
        offset += to_free->len;
    }

    return true;
}

static double cross_correlation(const int16_t* target, size_t t_len, const int16_t* ad, size_t a_len, size_t pos) {
    double sum = 0;
    for (size_t i = 0; i < a_len; i++) {
        if (pos + i >= t_len) break;
        sum += (double)target[pos + i] * ad[i];
    }
    return sum;
}

static double autocorrelation(const int16_t* ad, size_t a_len) {
    return cross_correlation(ad, a_len, ad, a_len, 0);
}

char* tr_identify(const struct sound_seg* target, const struct sound_seg* ad) {
    if (!target || !ad || !target->head || !ad->head) return strdup("");

    size_t t_len = tr_length((struct sound_seg*)target);
    size_t a_len = tr_length((struct sound_seg*)ad);
    if (a_len > t_len) return strdup("");

    int16_t* t_data = malloc(t_len * sizeof(int16_t));
    int16_t* a_data = malloc(a_len * sizeof(int16_t));
    tr_read((struct sound_seg*)target, t_data, 0, t_len);
    tr_read((struct sound_seg*)ad, a_data, 0, a_len);

    double ref = autocorrelation(a_data, a_len);
    if (ref == 0) {
        free(t_data);
        free(a_data);
        return strdup("");
    }

    char* result = malloc(1024); // Arbitrary max size for simplicity
    result[0] = '\0';
    size_t result_len = 0;

    for (size_t i = 0; i <= t_len - a_len; i++) {
        double corr = cross_correlation(t_data, t_len, a_data, a_len, i);
        if (corr >= 0.95 * ref) {
            char temp[50];
            snprintf(temp, 50, "%zu,%zu\n", i, i + a_len - 1);
            if (result_len + strlen(temp) < 1024) {
                strcat(result, temp);
                result_len += strlen(temp);
            }
            i += a_len - 1; // Skip overlapping regions
        }
    }

    // Remove trailing newline if present
    if (result_len > 0 && result[result_len - 1] == '\n') {
        result[result_len - 1] = '\0';
    }

    free(t_data);
    free(a_data);
    return result;
}

void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track, size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || srcpos + len > src_track->total_len || destpos > dest_track->total_len) return;

    // Find source portion at srcpos
    struct portion* src_current = src_track->head;
    size_t src_offset = 0;
    while (src_current && src_offset + src_current->len <= srcpos) {
        src_offset += src_current->len;
        src_current = src_current->next;
    }
    if (!src_current) return; // srcpos beyond track (shouldn’t happen due to total_len check)

    // Create new portion for insertion (shared backing)
    struct portion* new_portion = malloc(sizeof(struct portion));
    if (!new_portion) return;
    new_portion->data = src_current->data + (srcpos - src_offset);
    new_portion->len = len;
    new_portion->owned = false; // Shares parent’s data
    new_portion->parent = src_current;
    new_portion->child_count = 0;
    new_portion->next = NULL;
    src_current->child_count++; // Register as child of source portion

    // Handle empty destination track
    if (!dest_track->head) {
        dest_track->head = new_portion;
        dest_track->total_len = len;
        return;
    }

    // Find insertion point in dest_track
    struct portion* current = dest_track->head;
    struct portion* prev = NULL;
    size_t offset = 0;
    while (current && offset + current->len <= destpos) {
        offset += current->len;
        prev = current;
        current = current->next;
    }

    if (!current && offset < destpos) return; // destpos beyond track (shouldn’t happen)

    if (!current || offset == destpos) {
        // Insert at end or between portions
        new_portion->next = current;
        if (prev) {
            prev->next = new_portion;
        } else {
            dest_track->head = new_portion;
        }
        dest_track->total_len += len;
    } else {
        // Split current portion at destpos
        size_t split_pos = destpos - offset;
        if (split_pos > 0) {
            struct portion* after = malloc(sizeof(struct portion));
            if (!after) {
                free(new_portion);
                src_current->child_count--;
                return;
            }
            after->data = current->data + split_pos;
            after->len = current->len - split_pos;
            after->owned = current->owned;
            after->parent = current->parent;
            after->child_count = current->child_count;
            after->next = current->next;

            current->len = split_pos;
            current->owned = true; // Current now owns its truncated data
            current->child_count = 0; // Split portion has no children yet
            current->next = new_portion;
            new_portion->next = after;

            // If after inherits ownership, adjust original
            if (after->owned && after->parent) {
                after->owned = false; // Shares with parent
                after->parent->child_count++;
            }
        } else {
            // Insert at start of current portion
            new_portion->next = current;
            if (prev) {
                prev->next = new_portion;
            } else {
                dest_track->head = new_portion;
            }
        }
        dest_track->total_len += len;
    }
}


int main(int argc, char** argv) {
    //SEED=3231012018
    // struct sound_seg* s0 = tr_init();
    // tr_write(s0, ((int16_t[]){-16,-4,-4,-17,8,1,-9,-6,20,-15}), 0, 10);
    // struct sound_seg* s1 = tr_init();
    // tr_write(s1, ((int16_t[]){-20,14,-1,14,-16,18,9,-18,5,8,10,-2,13,-12,-12,-2,-14,-18,-8,-13,-6,15,18,-11,10,-9,-15,7,17,8,6,-8,-20,-13,4,-1}), 0, 36);
    // struct sound_seg* s2 = tr_init();
    // tr_write(s2, ((int16_t[]){-17,7}), 0, 2);
    // tr_delete_range(s0, 4, 3); //expect return True
    // tr_insert(s1, s1, 6, 15, 14);
    // tr_write(s0, ((int16_t[]){6,17,6,-12,-4,-18,-11}), 0, 7);
    // tr_write(s2, ((int16_t[]){1,14}), 0, 2);
    //  tr_write(s1, ((int16_t[]){-2,-2,-2,-2,-2,20,-2,-2,-2,-2,-2,-2,-2,-2,-2,3,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,9,-2,-2,-2,6,-2,-2,-2,8,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2}), 0, 50);
    //  size_t FAILING_LEN = tr_length(s1); //expected 50, actual 64
    // printf("%zu\n", FAILING_LEN);
    //  tr_destroy(s0);
    //  tr_destroy(s1);
    //  tr_destroy(s2);
}