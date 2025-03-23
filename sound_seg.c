#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

//NOTE: Buffer is int16
#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44
struct shared_buffer {
    int16_t* data;
    size_t refcount;
};
struct sound_seg_node {
    struct sound_seg_node* next;
    struct shared_buffer* buffer;
    size_t offset;
    size_t length;
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

double compute_cross_relation(int16_t *array1 , int16_t *array2 , size_t len) {
    double sum1 = 0;
    double sum2 = 0;
    for (size_t i = 0; i < len; i++) {
        sum1 += (double)(array1[i]) * (array2[i]);
        sum2 += (double)(array2[i]) * (array2[i]);
    }
    return sum2 == 0 ? 0 : sum1 / sum2;
}

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
    struct sound_seg* seg = malloc(sizeof(struct sound_seg));
    seg->head = NULL;
    seg->total_number_of_segments = 0;
    return seg;
}

void tr_destroy(struct sound_seg* track) {
    struct sound_seg_node* current = track->head;
    while (current) {
        struct sound_seg_node* next = current->next;
        if (--current->buffer->refcount == 0) {
            free(current->buffer->data);
            free(current->buffer);
        }
        free(current);
        current = next;
    }
    free(track);
}

size_t tr_length(struct sound_seg* seg) {
    size_t total = 0;
    struct sound_seg_node* n = seg->head;
    while (n != NULL) {
        total += n->length;
        n = n->next;
    }
    return total;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    size_t skipped = 0;
    size_t copied = 0;
    struct sound_seg_node* n = track->head;
    while (n != NULL && copied < len) {
        if (skipped + n->length <= pos) {
            skipped += n->length;
            n = n->next;
            continue;
        }
        size_t start = 0;
        if (pos > skipped) {
            start = pos - skipped;
        }
        size_t to_copy = n->length - start;
        if (to_copy > len - copied) {
            to_copy = len - copied;
        }
        for (size_t i = 0; i < to_copy; i++) {
            dest[copied] = n->buffer->data[n->offset + start + i];
            copied++;
        }
        skipped += n->length;
        n = n->next;
    }
}

void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    size_t skipped = 0;
    size_t written = 0;
    struct sound_seg_node* last = NULL;
    struct sound_seg_node* n = track->head;

    while (n != NULL && written < len) {
        if (skipped + n->length <= pos) {
            skipped += n->length;
            last = n;
            n = n->next;
            continue;
        }
        size_t offset = 0;
        if (pos > skipped) {
            offset = pos - skipped;
        }
        size_t writable = n->length - offset;
        size_t to_write = len - written;
        if (writable < to_write) {
            to_write = writable;
        }
        for (size_t i = 0; i < to_write; i++) {
            n->buffer->data[n->offset + offset + i] = src[written];
            written++;
        }
        skipped += n->length;
        last = n;
        n = n->next;
    }

    if (written < len) {
        struct shared_buffer* buf = malloc(sizeof(struct shared_buffer));
        buf->data = malloc((len - written) * sizeof(int16_t));
        for (size_t i = 0; i < len - written; i++) {
            buf->data[i] = src[written + i];
        }
        buf->refcount = 1;

        struct sound_seg_node* node = malloc(sizeof(struct sound_seg_node));
        node->buffer = buf;
        node->offset = 0;
        node->length = len - written;
        node->next = NULL;

        if (last == NULL) {
            track->head = node;
        } else {
            last->next = node;
        }
        track->total_number_of_segments++;
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    size_t skipped = 0;
    struct sound_seg_node* current = track->head;
    struct sound_seg_node* prev = NULL;

    while (current != NULL && len > 0) {
        size_t seg_start = skipped;
        size_t seg_end = seg_start + current->length;

        if (seg_end <= pos) {
            skipped = seg_end;
            prev = current;
            current = current->next;
            continue;
        }

        size_t offset = 0;
        if (pos > skipped) {
            offset = pos - skipped;
        }
        size_t deletable = current->length - offset;
        size_t to_delete = len;
        if (deletable < to_delete) {
            to_delete = deletable;
        }

        for (size_t i = offset; i + to_delete < current->length; i++) {
            current->buffer->data[current->offset + i] = current->buffer->data[current->offset + i + to_delete];
        }
        current->length -= to_delete;
        len -= to_delete;
        skipped = seg_start + current->length;

        if (current->length == 0 && current != track->head) {
            if (prev != NULL) {
                prev->next = current->next;
            }
            current->buffer->refcount--;
            if (current->buffer->refcount == 0) {
                free(current->buffer->data);
                free(current->buffer);
            }
            free(current);
            if (prev != NULL) {
                current = prev->next;
            } else {
                current = track->head;
            }
            track->total_number_of_segments--;
        } else {
            prev = current;
            current = current->next;
        }
    }
    return true;
}

char* tr_identify(struct sound_seg* target, struct sound_seg* ad) {
    size_t len_target = tr_length(target);
    size_t len_ad = tr_length(ad);
    if (len_target < len_ad) return strdup("");

    int16_t* buf_target = malloc(len_target * sizeof(int16_t));
    int16_t* buf_ad = malloc(len_ad * sizeof(int16_t));
    tr_read(target, buf_target, 0, len_target);
    tr_read(ad, buf_ad, 0, len_ad);

    double threshold = 0.95;
    size_t result_buf_size = 1024;
    char* result = calloc(result_buf_size, sizeof(char));
    result[0] = '\0';

    for (size_t i = 0; i <= len_target - len_ad; ++i) {
        double score = compute_cross_relation(&buf_target[i], buf_ad, len_ad);
        if (score >= threshold) {
            char entry[64];
            snprintf(entry, sizeof(entry), "%zu,%zu\n", i, i + len_ad - 1);
            if (strlen(result) + strlen(entry) + 1 >= result_buf_size) {
                result_buf_size *= 2;
                result = realloc(result, result_buf_size);
            }
            strcat(result, entry);
        }
    }

    free(buf_target);
    free(buf_ad);

    if (strlen(result) == 0) {
        free(result);
        return strdup("");
    }
    result[strlen(result) - 1] = '\0';
    return result;
}

void tr_insert(struct sound_seg* src, struct sound_seg* dest, size_t destpos, size_t srcpos, size_t len) {
    size_t skipped = 0;
    struct sound_seg_node* node = src->head;
    while (node && skipped + node->length <= srcpos) {
        skipped += node->length;
        node = node->next;
    }
    size_t remaining = len;
    size_t src_offset = srcpos - skipped;
    struct sound_seg_node* insert_head = NULL;
    struct sound_seg_node* insert_tail = NULL;
    while (node && remaining > 0) {
        size_t available = node->length - src_offset;
        size_t to_copy = (available < remaining) ? available : remaining;
        struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
        new_node->buffer = node->buffer;
        new_node->offset = node->offset + src_offset;
        new_node->length = to_copy;
        new_node->next = NULL;
        new_node->buffer->refcount++;
        if (insert_head == NULL) {
            insert_head = new_node;
            insert_tail = new_node;
        } else {
            insert_tail->next = new_node;
            insert_tail = new_node;
        }

        remaining -= to_copy;
        node = node->next;
        src_offset = 0;
    }
    skipped = 0;
    struct sound_seg_node* curr = dest->head;
    struct sound_seg_node* prev = NULL;
    while (curr && skipped + curr->length < destpos) {
        skipped += curr->length;
        prev = curr;
        curr = curr->next;
    }
    size_t offset = destpos - skipped;
    if (curr && offset > 0) {
        struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
        new_node->buffer = curr->buffer;
        new_node->offset = curr->offset;
        new_node->length = offset;
        new_node->next = insert_head;
        new_node->buffer->refcount++;
        if (prev) {
            prev->next = new_node;
        } else {
            dest->head = new_node;
        }
        prev = new_node;
        curr->offset += offset;
        curr->length -= offset;
    } else if (prev) {
        prev->next = insert_head;
    } else {
        dest->head = insert_head;
    }
    if (insert_tail) {
        insert_tail->next = curr;
    }
    while (insert_head) {
        dest->total_number_of_segments++;
        insert_head = insert_head->next;
    }
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
