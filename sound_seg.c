#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

//NOTE: Buffer is int16
#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44

struct segment_node {
    int16_t* data;
    size_t length;
    bool is_parent;
    size_t child_count;
    struct segment_node* next;
};

struct sound_seg {
    struct segment_node* head;
    size_t total_length;
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

struct sound_seg* tr_init() {
    struct sound_seg* track = malloc(sizeof(struct sound_seg));
    if (track) {
        track->head = NULL;
        track->total_length = 0;
    }
    return track;
}

// Helper function to free a segment node
static void free_segment_node(struct segment_node* node) {
    if (node) {
        if (!node->is_parent && node->data) {
            free(node->data);
        }
        free(node);
    }
}

// Destroy a sound_seg object and free all allocated memory
void tr_destroy(struct sound_seg* track) {
    if (!track) return;

    struct segment_node* current = track->head;
    while (current) {
        struct segment_node* next = current->next;
        free_segment_node(current);
        current = next;
    }
    free(track);
}

// Return the length of the segment
size_t tr_length(struct sound_seg* track) {
    return track ? track->total_length : 0;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !track->head || !dest) return;
    size_t current_pos = 0;
    struct segment_node* current = track->head;
    size_t dest_idx = 0;
    while (current && current_pos + current->length <= pos) {
        current_pos += current->length;
        current = current->next;
    }
    while (current && dest_idx < len) {
        size_t offset = pos - current_pos;
        size_t available = current->length - offset;
        size_t to_copy = len - dest_idx < available ? len - dest_idx : available;
        memcpy(dest + dest_idx, current->data + offset, to_copy * sizeof(int16_t));
        dest_idx += to_copy;
        current_pos += current->length;
        current = current->next;
    }
}

void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;

    // If track is empty, create first node
    if (!track->head) {
        struct segment_node* node = malloc(sizeof(struct segment_node));
        node->data = malloc(len * sizeof(int16_t));
        node->length = len;
        node->is_parent = false;
        node->child_count = 0;
        node->next = NULL;
        memcpy(node->data, src, len * sizeof(int16_t));
        track->head = node;
        track->total_length = pos + len;
        return;
    }

    // Find insertion point
    struct segment_node* prev = NULL;
    struct segment_node* current = track->head;
    size_t current_pos = 0;

    while (current && current_pos + current->length <= pos) {
        current_pos += current->length;
        prev = current;
        current = current->next;
    }

    // Create new node for the data
    struct segment_node* new_node = malloc(sizeof(struct segment_node));
    new_node->data = malloc(len * sizeof(int16_t));
    new_node->length = len;
    new_node->is_parent = false;
    new_node->child_count = 0;
    memcpy(new_node->data, src, len * sizeof(int16_t));

    if (!current) {  // Append to end
        new_node->next = NULL;
        if (prev) {
            prev->next = new_node;
        } else {
            track->head = new_node;
        }
        track->total_length = pos + len;
    } else {  // Insert in middle
        size_t offset = pos - current_pos;
        if (offset == 0) {  // Insert before current
            new_node->next = current;
            if (prev) {
                prev->next = new_node;
            } else {
                track->head = new_node;
            }
        } else {  // Split current node
            struct segment_node* tail = malloc(sizeof(struct segment_node));
            tail->data = current->data + offset;
            tail->length = current->length - offset;
            tail->is_parent = current->is_parent;
            tail->child_count = current->child_count;
            tail->next = current->next;

            current->length = offset;
            current->next = new_node;
            new_node->next = tail;
        }
        track->total_length += len;
    }
}

// Delete a range of elements from the track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || !track->head || len == 0) return true;

    struct segment_node* prev = NULL;
    struct segment_node* current = track->head;
    size_t current_pos = 0;

    while (current && current_pos + current->length <= pos) {
        current_pos += current->length;
        prev = current;
        current = current->next;
    }

    if (!current) return true;

    size_t offset = pos - current_pos;
    size_t remaining = len;

    // Check if any segment to be deleted is a parent with children
    struct segment_node* check = current;
    size_t check_pos = current_pos;
    while (check && remaining > 0) {
        if (check->is_parent && check->child_count > 0) {
            return false;
        }
        size_t segment_remaining = check->length - (check_pos - current_pos);
        remaining = remaining > segment_remaining ? remaining - segment_remaining : 0;
        check_pos += check->length;
        check = check->next;
    }

    // Perform deletion
    remaining = len;
    while (current && remaining > 0) {
        if (offset > 0) {  // Split at start
            if (offset >= current->length) {
                prev = current;
                current = current->next;
                offset -= current->length;
                continue;
            }
            if (offset + remaining >= current->length) {
                size_t tail_len = current->length - offset;
                current->length = offset;
                prev = current;
                current = current->next;
                remaining -= tail_len;
                offset = 0;
            } else {
                struct segment_node* tail = malloc(sizeof(struct segment_node));
                tail->data = current->data + offset + remaining;
                tail->length = current->length - offset - remaining;
                tail->is_parent = current->is_parent;
                tail->child_count = current->child_count;
                tail->next = current->next;
                current->length = offset;
                current->next = tail;
                remaining = 0;
            }
        } else {  // Delete whole segments or trim from start
            if (remaining >= current->length) {
                struct segment_node* to_free = current;
                current = current->next;
                if (prev) {
                    prev->next = current;
                } else {
                    track->head = current;
                }
                remaining -= to_free->length;
                free_segment_node(to_free);
            } else {
                current->data += remaining;
                current->length -= remaining;
                remaining = 0;
            }
        }
    }

    track->total_length -= len;
    return true;
}
double compute_cross_relation(int16_t *array1 , int16_t *array2 , size_t len) {
    double sum1 = 0;
    double sum2 = 0;
    int16_t *ptr1 = array1;
    int16_t *ptr2 = array2;
    for (size_t i = 0; i < len; i++) {
        sum1 += (double) (*ptr1)*(*ptr2);
        sum2 += (double) (*ptr2)*(*ptr2);
        ptr1++;
        ptr2++;
    }
    return sum1/sum2;
}

// Returns a string containing <start>,<end> ad pairs in target
// char* tr_identify(struct sound_seg* target, struct sound_seg* ad){
//     size_t temp_size = 256;
//     char* ptr = (char*)malloc(temp_size);
//     if (ptr == NULL) {
//         return NULL;
//     }
//     ptr[0] = '\0';
//     double threshold = 0.95;
//     size_t current_length = 0;
//     for (int i = 0; i <= target->length - ad->length; i++) {
//         int found = 1;
//         double cross_relation = compute_cross_relation(&target->ptr[i], ad->ptr, ad->length);
//         if (cross_relation < threshold) {
//             found = 0;
//         }
//         if (found == 1) { //found
//             size_t last_index = i + ad->length - 1;
//             char temp[32];
//             snprintf(temp, sizeof(temp), "%zu,%zu\n", i, last_index);
//             size_t length = strlen(temp);
//             if (current_length + length + 1 >= temp_size) {
//                 temp_size *= 2;
//                 char* temp_ptr = (char*)realloc(ptr, temp_size);
//                 if (!temp_ptr) {
//                     free(ptr);
//                     return NULL;
//                 }
//                 ptr = temp_ptr;
//             }
//             strcat(ptr, temp);
//             current_length += length;
//         }
//     }
//     if (current_length == 0) {
//         free(ptr);
//         char *str = "";
//         return str;
//     }
//     if (ptr[strlen(ptr) - 1] == '\n') {
//         ptr[strlen(ptr) - 1] = '\0';
//     }
//     return ptr;
// }
char* tr_identify(const struct sound_seg* target, const struct sound_seg* ad) {
    size_t target_len = tr_length((struct sound_seg*)target);
    size_t ad_len = tr_length((struct sound_seg*)ad);
    if (target_len < ad_len) return strdup("");

    int16_t* target_data = malloc(target_len * sizeof(int16_t));
    int16_t* ad_data = malloc(ad_len * sizeof(int16_t));
    tr_read((struct sound_seg*)target, target_data, 0, target_len);
    tr_read((struct sound_seg*)ad, ad_data, 0, ad_len);
    char* result = malloc(256);
    size_t capacity = 256;
    size_t used = 0;
    result[0] = '\0';
    for (size_t i = 0; i <= target_len - ad_len; i++) {
        double corr = compute_cross_relation(target_data + i, ad_data, ad_len);
        if (corr >= 0.95) {
            char temp[32];
            snprintf(temp, sizeof(temp), "%zu,%zu\n", i, i + ad_len - 1);
            size_t len = strlen(temp);
            if (used + len + 1 > capacity) {
                capacity *= 2;
                result = realloc(result, capacity);
            }
            strcat(result, temp);
            used += len;
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

void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track,
              size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || len == 0) return;

    // Find source position
    struct segment_node* src_current = src_track->head;
    size_t src_current_pos = 0;
    while (src_current && src_current_pos + src_current->length <= srcpos) {
        src_current_pos += src_current->length;
        src_current = src_current->next;
    }
    if (!src_current) return;

    size_t src_offset = srcpos - src_current_pos;
    if (src_offset + len > src_current->length) return;  // Simplified: assuming single segment

    // Find destination position
    struct segment_node* dest_prev = NULL;
    struct segment_node* dest_current = dest_track->head;
    size_t dest_current_pos = 0;
    while (dest_current && dest_current_pos + dest_current->length <= destpos) {
        dest_current_pos += dest_current->length;
        dest_prev = dest_current;
        dest_current = dest_current->next;
    }

    // Create new node with shared reference
    struct segment_node* new_node = malloc(sizeof(struct segment_node));
    new_node->data = src_current->data + src_offset;
    new_node->length = len;
    new_node->is_parent = false;
    new_node->child_count = 0;
    src_current->is_parent = true;
    src_current->child_count++;

    // Insert into destination
    if (!dest_current) {  // Append to end
        new_node->next = NULL;
        if (dest_prev) {
            dest_prev->next = new_node;
        } else {
            dest_track->head = new_node;
        }
    } else {  // Insert in middle
        size_t dest_offset = destpos - dest_current_pos;
        if (dest_offset == 0) {
            new_node->next = dest_current;
            if (dest_prev) {
                dest_prev->next = new_node;
            } else {
                dest_track->head = new_node;
            }
        } else {
            struct segment_node* tail = malloc(sizeof(struct segment_node));
            tail->data = dest_current->data + dest_offset;
            tail->length = dest_current->length - dest_offset;
            tail->is_parent = dest_current->is_parent;
            tail->child_count = dest_current->child_count;
            tail->next = dest_current->next;

            dest_current->length = dest_offset;
            dest_current->next = new_node;
            new_node->next = tail;
        }
    }
    dest_track->total_length += len;
}
int main(int argc, char** argv) {
    struct sound_seg* s0 = tr_init();
    tr_write(s0, ((int16_t[]){-1,8,-10,7,13,0,0,6,5,-14,3,13,-9,12,12,-1}), 0, 16);
    struct sound_seg* s1 = tr_init();
    tr_write(s1, ((int16_t[]){-12,9,11,2,1}), 0, 5);
    struct sound_seg* s2 = tr_init();
    tr_write(s2, ((int16_t[]){-3}), 0, 1);
    struct sound_seg* s3 = tr_init();
    tr_write(s3, ((int16_t[]){2}), 0, 1);
    tr_delete_range(s0, 10, 2); //expect return True
    size_t FAILING_LEN = tr_length(s0); //expected 14, actual 10
    tr_destroy(s0);
    tr_destroy(s1);
    tr_destroy(s2);
    tr_destroy(s3);
}