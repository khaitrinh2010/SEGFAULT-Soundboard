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
        if (current->audio_data != NULL) {
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
    struct sound_seg_node *current = track->head;
    size_t skipped_length = 0;
    size_t prev = 0;
    size_t index_in_src = 0;
    size_t count_index_in_current_segment = 0;

    // Initialize first segment if needed
    if (current->audio_data == NULL) {
        size_t new_size = pos + len;
        current->audio_data = malloc(sizeof(int16_t) * new_size);
        if (!current->audio_data) return;
        for (size_t i = 0; i < new_size; i++) {
            current->audio_data[i] = 0;
        }
        current->length_of_the_segment = new_size;
    }

    while (current != NULL) {
        size_t current_length = current->length_of_the_segment;

        if (skipped_length + current_length <= pos && track->total_number_of_segments > 1) {
            skipped_length += current_length;
            current = current->next;
            continue;
        }
        size_t offset = 0;
        if (pos > skipped_length) {
            offset = pos - skipped_length;
        }
        int16_t *first_ptr = current->audio_data + offset;
        size_t available = current_length - offset;
        size_t remaining = len - prev;
        size_t to_write = (available < remaining) ? available : remaining;
        for (size_t i = 0; i < to_write; i++) {
            first_ptr[i] = src[prev + i];
            index_in_src++;
            count_index_in_current_segment++;
        }
        prev = index_in_src;
        if (index_in_src < len) {
            if (current->next == NULL) {
                size_t new_size = pos + len;
                int16_t *temp_ptr = (int16_t *) realloc(current->audio_data, sizeof(int16_t) * new_size);
                if (!temp_ptr) return;

                for (size_t i = current_length; i < new_size; i++) {
                    temp_ptr[i] = 0;
                }
                current->audio_data = temp_ptr;
                current->length_of_the_segment = new_size;
                int16_t *restart_position = current->audio_data + offset + count_index_in_current_segment;
                for (size_t i = prev; i < len; i++) {
                    *restart_position = src[i];
                    restart_position++;
                }
                return;
            } else {
                current = current->next;
                skipped_length += current->length_of_the_segment;
                count_index_in_current_segment = 0;
            }
        } else {
            return;
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
        size_t offset = pos > skipped ? pos - skipped : 0;
        size_t deletable = current->length_of_the_segment - offset;
        size_t to_delete = (deletable > len) ? len : deletable;

        if (offset + to_delete < current->length_of_the_segment) {
            memmove(current->audio_data + offset,
                    current->audio_data + offset + to_delete,
                    (current->length_of_the_segment - offset - to_delete) * sizeof(int16_t));
        }
        current->length_of_the_segment -= to_delete;
        len -= to_delete;
        skipped = seg_start + current->length_of_the_segment;
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
    size_t skipped_src = 0;
    struct sound_seg_node* cur_src = src->head;
    while (cur_src && skipped_src + cur_src->length_of_the_segment <= srcpos) {
        skipped_src += cur_src->length_of_the_segment;
        cur_src = cur_src->next;
    }
    if (!cur_src) return;
    size_t offset_in_src = srcpos - skipped_src;
    int16_t* buffer = malloc(len * sizeof(int16_t));
    if (!buffer) return;
    size_t written = 0;
    struct sound_seg_node* temp = cur_src;
    while (temp && written < len) {
        size_t i = (temp == cur_src) ? offset_in_src : 0;
        while (i < temp->length_of_the_segment && written < len)
            buffer[written++] = temp->audio_data[i++];
        temp = temp->next;
    }
    struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
    new_node->audio_data = buffer;
    new_node->length_of_the_segment = len;
    new_node->next = NULL;
    size_t skipped_dest = 0;
    struct sound_seg_node* cur = dest->head;
    struct sound_seg_node* prev = NULL;

    while (cur && skipped_dest + cur->length_of_the_segment <= destpos) {
        skipped_dest += cur->length_of_the_segment;
        prev = cur;
        cur = cur->next;
    }
    size_t offset_dest = destpos - skipped_dest;
    if (!cur) {
        if (prev) prev->next = new_node;
    } else if (offset_dest == 0) {
        if (prev) {
            prev->next = new_node;
            new_node->next = cur;
        } else {
            new_node->next = dest->head;
            dest->head = new_node;
        }
    } else {
        struct sound_seg_node* split = malloc(sizeof(struct sound_seg_node));
        size_t split_len = cur->length_of_the_segment - offset_dest;
        split->audio_data = malloc(sizeof(int16_t) * split_len);
        for (size_t i = 0; i < split_len; i++)
            split->audio_data[i] = cur->audio_data[offset_dest + i];
        split->length_of_the_segment = split_len;
        split->next = cur->next;

        int16_t* new_audio = malloc(sizeof(int16_t) * offset_dest);
        for (size_t i = 0; i < offset_dest; i++)
            new_audio[i] = cur->audio_data[i];
        free(cur->audio_data);
        cur->audio_data = new_audio;
        cur->length_of_the_segment = offset_dest;
        cur->next = new_node;
        new_node->next = split;
    }
    dest->total_number_of_segments++;
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