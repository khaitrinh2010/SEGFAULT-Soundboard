#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

//NOTE: Buffer is int16
#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44

//SIGNAL 11: occurs when program attempts to access memory it does not have permission to access
struct sound_seg_node {
    struct sound_seg_node* next;
    size_t length_of_the_segment;  // How many samples from this segment
    int16_t* audio_data; //start element in the buffer
    bool owns_data; //if this node should free the memory
};

struct sound_seg {
    struct sound_seg_node* head;
    size_t total_number_of_segments;
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

// Initialize a new sound_seg object
struct sound_seg* tr_init() {
    //When init, only needs 1 segment, more segment will be added in the insertion
    struct sound_seg_node *node = (struct sound_seg_node *)malloc(sizeof(struct sound_seg_node));
    node->next = NULL;
    node->length_of_the_segment = 0;
    node->audio_data = NULL;

    struct sound_seg *seg = (struct sound_seg *)malloc(sizeof(struct sound_seg));
    seg->head = node;
    seg->total_number_of_segments = 1; //initially, there is only 1 segment in a track
    return seg;

}
void tr_destroy(struct sound_seg* obj) {
    struct sound_seg_node* current = obj->head;
    while (current != NULL) {
        struct sound_seg_node* next = current->next;
        if (current->owns_data && current->audio_data != NULL) {
            free(current->audio_data);
        }
        free(current);
        current = next;
    }
    free(obj);
}

// Return the length of the segment
size_t tr_length(struct sound_seg* seg) {
    size_t result = 0;
    struct sound_seg_node *current = seg->head;
    while (current != NULL) {
        result += current->length_of_the_segment;
        current = current->next;
    }
    return result;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    size_t skipped = 0, written = 0;
    struct sound_seg_node* current = track->head;
    while (current && written < len) {
        if (skipped + current->length_of_the_segment <= pos) {
            skipped += current->length_of_the_segment;
            current = current->next;
            continue;
        }
        size_t start = (pos > skipped) ? (pos - skipped) : 0;
        for (size_t i = start; i < current->length_of_the_segment && written < len; i++) {
            dest[written++] = current->audio_data[i];
        }
        skipped += current->length_of_the_segment;
        current = current->next;
    }
}

void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    size_t skipped = 0, written = 0;
    struct sound_seg_node* cur = track->head;

    while (cur && written < len) {
        size_t seg_len = cur->length_of_the_segment;
        size_t seg_start = skipped;
        size_t seg_end = seg_start + seg_len;
        if (seg_end <= pos) {
            skipped = seg_end;
            cur = cur->next;
            continue;
        }
        size_t offset = (pos > skipped) ? (pos - skipped) : 0;
        size_t available = seg_len > offset ? seg_len - offset : 0;
        size_t to_write = (len - written < available) ? (len - written) : available;
        for (size_t i = 0; i < to_write; i++) {
            cur->audio_data[offset + i] = src[written++];
        }
        skipped = seg_end;
        cur = cur->next;
    }
    if (written < len) {
        struct sound_seg_node* last = track->head;
        while (last->next) last = last->next;
        size_t new_len = pos + len;
        int16_t* new_data = realloc(last->audio_data, new_len * sizeof(int16_t));
        if (!new_data) return;
        for (size_t i = last->length_of_the_segment; i < new_len; i++) {
            new_data[i] = 0;  // zero padding
        }
        last->audio_data = new_data;
        last->length_of_the_segment = new_len;
        for (; written < len; written++) {
            last->audio_data[pos + written] = src[written];
        }
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    size_t skipped = 0;
    struct sound_seg_node* current = track->head;
    struct sound_seg_node* prev = NULL;
    while (current && len > 0) {
        size_t seg_start = skipped;
        size_t seg_end = seg_start + current->length_of_the_segment;
        if (seg_end <= pos) {
            skipped = seg_end;
            prev = current;
            current = current->next;
            continue;
        }
        size_t offset = (pos > skipped) ? pos - skipped : 0;
        size_t deletable = current->length_of_the_segment - offset;
        size_t to_delete = (len < deletable) ? len : deletable;

        // Shift remaining samples manually
        for (size_t i = offset; i + to_delete < current->length_of_the_segment; i++) {
            current->audio_data[i] = current->audio_data[i + to_delete];
        }
        current->length_of_the_segment -= to_delete;
        len -= to_delete;
        skipped = seg_end;
        if (current->length_of_the_segment == 0 && current != track->head) {
            if (prev) prev->next = current->next;
            free(current->audio_data);
            free(current);
            current = (prev) ? prev->next : track->head;
            track->total_number_of_segments--;
        } else {
            prev = current;
            current = current->next;
        }
    }

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

void tr_insert(struct sound_seg* src, struct sound_seg* dest, size_t destpos, size_t srcpos, size_t len) {
    if (!src || !dest || len == 0) return;
    struct sound_seg_node* cur_src = src->head;
    size_t skipped = 0;
    cur_src->owns_data = true;
    while (cur_src && skipped + cur_src->length_of_the_segment <= srcpos) {
        skipped += cur_src->length_of_the_segment;
        cur_src = cur_src->next;
    }
    if (!cur_src) return;
    size_t offset = srcpos - skipped;
    if (offset + len > cur_src->length_of_the_segment) return;
    int16_t* shared_ptr = cur_src->audio_data + offset;
    if (offset > 0) {
        struct sound_seg_node* before = malloc(sizeof(struct sound_seg_node));
        before->audio_data = cur_src->audio_data;
        before->length_of_the_segment = offset;
        if (before->audio_data == src->head->audio_data) {
            before->owns_data = true;
        }
        else {
            before->owns_data = false;
        }
        before->next = cur_src;
        cur_src->audio_data = cur_src->audio_data + offset;
        cur_src->length_of_the_segment -= offset;
        cur_src->owns_data = false;
        if (src->head == cur_src) {
            src->head = before;
        } else {
            struct sound_seg_node* walker = src->head;
            while (walker->next != cur_src) walker = walker->next;
            walker->next = before;
        }
        src->total_number_of_segments++;
    }
    if (cur_src->length_of_the_segment > len) {
        struct sound_seg_node* after = malloc(sizeof(struct sound_seg_node));
        after->audio_data = cur_src->audio_data + len;
        after->length_of_the_segment = cur_src->length_of_the_segment - len;
        after->owns_data = false;
        after->next = cur_src->next;
        cur_src->length_of_the_segment = len;
        cur_src->next = after;
        src->total_number_of_segments++;
    }
    struct sound_seg_node* dest_shared = malloc(sizeof(struct sound_seg_node));
    dest_shared->audio_data = cur_src->audio_data;
    dest_shared->length_of_the_segment = len;
    dest_shared->owns_data = false;
    dest_shared->next = NULL;
    struct sound_seg_node* cur_dest = dest->head;
    struct sound_seg_node* prev = NULL;
    size_t skipped_dest = 0;
    while (cur_dest && skipped_dest + cur_dest->length_of_the_segment <= destpos) {
        skipped_dest += cur_dest->length_of_the_segment;
        prev = cur_dest;
        cur_dest = cur_dest->next;
    }
    if (!prev) {
        dest_shared->next = dest->head;
        dest->head = dest_shared;
    } else {
        dest_shared->next = prev->next;
        prev->next = dest_shared;
    }

    dest->total_number_of_segments++;
}

int main(int argc, char** argv) {
    struct sound_seg* s0 = tr_init();
    tr_write(s0, ((int16_t[]){-12,12}), 0, 2);
    struct sound_seg* s1 = tr_init();
    tr_write(s1, ((int16_t[]){-6,11,7,-10,4,20,6,14}), 0, 8);
    struct sound_seg* s2 = tr_init();
    tr_write(s2, ((int16_t[]){-17,8,-7,0,-10}), 0, 5);
    tr_write(s2, ((int16_t[]){17,6,12}), 5, 3);
    tr_write(s1, ((int16_t[]){5}), 8, 1);
    tr_write(s2, ((int16_t[]){10,-7,17,17,17,15,3,17}), 0, 8);
    tr_write(s1, ((int16_t[]){19,-10,-16,-10,-6}), 9, 5);
    tr_insert(s0, s0, 1, 0, 1);
    tr_write(s2, ((int16_t[]){18,20,-8,-12,-10,-20,7,17}), 0, 8);
    tr_write(s0, ((int16_t[]){14,15,-5}), 0, 3);
    tr_write(s1, ((int16_t[]){-5,11,-18,5,3,-14,-9,-8,-15,0,-12,15,11,-17}), 0, 14);
    size_t FAILING_LEN = tr_length(s0); //expected 3, actual 4
    tr_destroy(s0);
}