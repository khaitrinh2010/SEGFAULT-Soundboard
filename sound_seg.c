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
    int16_t* sample;
    short ref_count;
    struct sound_seg_node* next;
    struct sound_seg_node* parent_node;
    bool owns_data;
};

struct sound_seg {
    struct sound_seg_node* head;
    size_t total_samples;
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
}

struct sound_seg* tr_init(void) {
    struct sound_seg* track = malloc(sizeof(struct sound_seg));
    if (!track) return NULL;
    track->head = NULL;
    track->total_samples = 0;
    return track;
}

void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    struct sound_seg_node* current = track->head;
    while (current) {
        struct sound_seg_node* next = current->next;
        if (current->owns_data) { // This node owns the sample
            free(current->sample);
        }
        free(current);
        current = next;
    }
    free(track);
}

// Return the total number of samples in the track
size_t tr_length(struct sound_seg* track) {
    return track ? track->total_samples : 0;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest || len == 0) return;
    struct sound_seg_node* current = track->head;
    size_t i = 0;
    while (current && i < pos) {
        current = current->next;
        i++;
    }
    for (size_t j = 0; j < len && current; j++) {
        dest[j] = *(current->sample);
        current = current->next;
    }
}

void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;
    struct sound_seg_node* current = track->head;
    struct sound_seg_node* prev = NULL;
    size_t i = 0;
    while (current && i < pos) {
        prev = current;
        current = current->next;
        i++;
    }
    while (i < pos) { //Create for sufficient nodes
        struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
        if (!new_node) return;
        new_node->sample = malloc(sizeof(int16_t));
        if (!new_node->sample) { free(new_node); return; }
        *new_node->sample = 0;
        new_node->ref_count = 0;
        new_node->next = NULL;
        new_node->parent_node = NULL;
        new_node->owns_data = true;
        if (prev) prev->next = new_node;
        else track->head = new_node;
        prev = new_node;
        track->total_samples++;
        i++;
    }
    for (size_t j = 0; j < len; j++) {
        if (current) {
            *current->sample = src[j];
            prev = current;
            current = current->next;
        } else {
            struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
            if (!new_node) return;
            new_node->sample = malloc(sizeof(int16_t));
            if (!new_node->sample) { free(new_node); return; }
            *new_node->sample = src[j];
            new_node->ref_count = 0;
            new_node->next = NULL;
            new_node->parent_node = NULL;
            new_node->owns_data = true;
            if (prev) prev->next = new_node;
            else track->head = new_node;
            prev = new_node;
            track->total_samples++;
        }
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || len == 0 || pos >= track->total_samples) return false;
    struct sound_seg_node* current = track->head;
    struct sound_seg_node* prev = NULL;
    size_t i = 0;
    while (current && i < pos) {
        prev = current;
        current = current->next;
        i++;
    }
    if (!current) return false; //out of bound
    struct sound_seg_node* check = current;
    for (size_t j = 0; j < len && check; j++) {
        if (check->ref_count > 0) {
            return false;
        }
        check = check->next;
    }

    for (size_t j = 0; j < len && current; j++) {
        struct sound_seg_node* next = current->next;
        if (current->parent_node != NULL) {
            current->parent_node->ref_count--;
        }
        if (current->owns_data) {
            free(current->sample);
        }
        free(current);
        current = next;
        track->total_samples--;
    }
    if (prev) prev->next = current;
    else track->head = current;

    return true;
}

double compute_cross_correlation(const int16_t* target, const int16_t* ad, size_t len) {
    double sum_product = 0.0;
    double sum_ad_sq = 0.0;
    for (size_t i = 0; i < len; i++) {
        sum_product += (double)target[i] * (double)ad[i];
        sum_ad_sq += (double)ad[i] * (double)ad[i];
    }
    return sum_product / sum_ad_sq; // Normalize by ad's autocorrelation
}

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
    tr_read((struct sound_seg*)target, target_data, 0, target_len);
    tr_read((struct sound_seg*)ad, ad_data, 0, ad_len);

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

void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track,
               size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || len == 0 || srcpos + len > tr_length(src_track)) return;
    struct sound_seg_node* src_current = src_track->head;
    size_t i = 0;
    while (src_current && i < srcpos) {
        src_current = src_current->next;
        i++;
    }
    if (!src_current) return;
    struct sound_seg_node* dest_current = dest_track->head;
    struct sound_seg_node* dest_prev = NULL;
    i = 0;
    while (dest_current && i < destpos) {
        dest_prev = dest_current;
        dest_current = dest_current->next;
        i++;
    }
    struct sound_seg_node* insert_head = NULL;
    struct sound_seg_node* insert_tail = NULL;
    struct sound_seg_node* src_temp = src_current;
    for (size_t j = 0; j < len && src_temp; j++) {
        struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
        if (!new_node) return;
        new_node->sample = src_temp->sample;
        new_node->ref_count = 0;
        new_node->next = NULL;
        new_node->owns_data = false;
        new_node->parent_node = src_temp;
        if (src_temp->ref_count == 0) {
            src_temp->ref_count = 1;
        }
        else {
            src_temp->ref_count++;
        }
        if (!insert_head) insert_head = insert_tail = new_node;
        else {
            insert_tail->next = new_node;
            insert_tail = new_node;
        }
        src_temp = src_temp->next;
    }

    if (insert_head) {
        insert_tail->next = dest_current;
        if (dest_prev) {
            dest_prev->next = insert_head;
        } else {
            dest_track->head = insert_head;
        }
        dest_track->total_samples += len;
    }

}

void print_track(struct sound_seg* track) {
    if (!track) {
        printf("Track is NULL\n");
        return;
    }

    printf("Track Metadata:\n");
    printf("Total Samples: %zu\n", track->total_samples);
    printf("Nodes:\n");

    struct sound_seg_node* current = track->head;
    int index = 0;
    if (!current) {
        printf("  (Empty track)\n");
        return;
    }

    while (current) {
        printf("  Node %d:\n", index);
        printf("    Sample Value: %d\n", *(current->sample));
        printf("    Ref Count: %d\n", current->ref_count);
        if (current->parent_node) {
            printf("    Parent Sample Value: %d\n", *(current->parent_node->sample));
        } else {
            printf("    Parent: None\n");
        }
        current = current->next;
        index++;
    }
    printf("\n");
}

int main(int argc, char** argv) {
    // struct sound_seg* s0 = tr_init();
    // tr_write(s0, ((int16_t[]){-8,-6,-13,17,13,-19,11,-1}), 0, 8);
    // tr_write(s0, ((int16_t[]){8,-15,12,-14,-17,15,-15,-10}), 0, 8);
    // tr_delete_range(s0, 3, 4); //expect return True
    // tr_insert(s0, s0, 3, 3, 1);
    //
    // tr_write(s0, ((int16_t[]){1,1,9,20,18}), 0, 5);
    // print_track(s0);
    //tr_delete_range(s0, 1, 3); //expect return True
}