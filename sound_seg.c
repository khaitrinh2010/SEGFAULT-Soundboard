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
#pragma pack(push, 1)
struct sound_seg_node {
    union A {
        struct  {
            int refCount;
            int16_t sample;
        } parent_data;
        struct {
            struct sound_seg_node *parent;
            int16_t* parent_data_address;
        }  child_data        ;
    } A;
    struct sound_seg_node* next;
    bool isParent;

};

struct sound_seg {
    struct sound_seg_node* head;
};
struct sound_seg_node* parent_node = NULL;
struct sound_seg_node* insert_head = NULL;
struct sound_seg_node* insert_tail = NULL;
#pragma pack(pop)


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

struct sound_seg* tr_init(void) {
    struct sound_seg* track = malloc(sizeof(struct sound_seg));
    if (!track) return NULL;
    track->head = NULL;
    return track;
}

void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    struct sound_seg_node* current = track->head;
    while (current) {
        struct sound_seg_node* next = current->next;
        free(current);
        current = next;
    }
    free(track);
}

// Return the total number of samples in the track
size_t tr_length(struct sound_seg* track) {
    size_t length = 0;
    struct sound_seg_node* current = track->head;
    while (current) {
        length++;
        current = current->next;
    }
    return length;
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
        dest[j] = current->isParent ? current->A.parent_data.sample : *(current->A.child_data.parent_data_address);
        current = current->next;
    }
}

void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track|| !src || len == 0) return;
    struct sound_seg_node* current = track->head;
    struct sound_seg_node* prev = NULL;
    size_t i = 0;

    while (current && i < pos) {
        prev = current;
        current = current->next; //Find the current node to start writting to
        i++;
    }

    while (i < pos) {
        struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
        if (!new_node) return;
        new_node->A.parent_data.sample = 0;
        new_node->A.parent_data.refCount = 0;
        new_node->next = NULL;
        new_node->isParent = true;
        if (prev) prev->next = new_node;
        else track->head = new_node;
        prev = new_node;
        i++;
    }

    for (size_t j = 0; j < len; j++) {
        if (current) {
            if (current->isParent) {
                current->A.parent_data.sample = src[j];
            }
            else {
                *(current->A.child_data.parent_data_address) = src[j];
            }
            prev = current;
            current = current->next;
        } else {
            struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
            if (!new_node) return;
            new_node->A.parent_data.sample = src[j];
            new_node->A.parent_data.refCount = 0;
            new_node->next = NULL;
            new_node->isParent = true;
            if (prev) prev->next = new_node;
            else track->head = new_node;
            prev = new_node;
        }
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || len == 0 || pos >= tr_length(track)) return false;
    struct sound_seg_node* current = track->head;
    struct sound_seg_node* prev = NULL;
    size_t i = 0;
    while (current && i < pos) {
        prev = current;
        current = current->next;
        i++;
    }
    if (!current) return false;
    struct sound_seg_node* check = current;
    for (size_t j = 0; j < len && check; j++) {
        if (check->isParent && check->A.parent_data.refCount > 0) {
            return false;
        }
        check = check->next;
    }
    for (size_t j = 0; j < len && current; j++) {
        struct sound_seg_node* next = current->next;
        if (!current->isParent && current->A.child_data.parent) {
            current->A.child_data.parent->A.parent_data.refCount--;
        }
        free(current);
        current = next;
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
    insert_head = NULL;
    insert_tail = NULL;
    struct sound_seg_node* src_temp = src_current;
    for (size_t j = 0; j < len && src_temp; j++) {
        struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
        if (!new_node) return;
        parent_node = src_temp;
        while (!parent_node->isParent && parent_node->A.child_data.parent) {
            parent_node = parent_node->A.child_data.parent;
        }
        new_node->A.child_data.parent_data_address = &parent_node->A.parent_data.sample;
        new_node->A.child_data.parent = parent_node;
        new_node->next = NULL;
        new_node->isParent = false;
        parent_node->A.parent_data.refCount++;
        if (!insert_head) insert_head = insert_tail = new_node;
        else {
            insert_tail->next = new_node;
            insert_tail = new_node;
        }
        src_temp = src_temp->next;
    }

    if (insert_head) {
        insert_tail->next = dest_current;
        if (dest_prev) dest_prev->next = insert_head;
        else dest_track->head = insert_head;
    }
}
int main(int argc, char** argv) {
    struct sound_seg* s0 = tr_init();
    tr_write(s0, ((int16_t[]){}), 0, 0);
    struct sound_seg* s1 = tr_init();
    tr_write(s1, ((int16_t[]){3,18,11,-8,5,-1,-18,-15,0,-6,-5,-14,4}), 0, 13);
    struct sound_seg* s2 = tr_init();
    tr_write(s2, ((int16_t[]){2,19,5,13,-10,-3}), 0, 6);
    struct sound_seg* s3 = tr_init();
    tr_write(s3, ((int16_t[]){-9,-5,20,-12,0,-18,-1,-19,-6}), 0, 9);
    tr_write(s3, ((int16_t[]){11,5,-2,7,-15,8,-13,-1,7}), 0, 9);
    tr_insert(s2, s1, 9, 1, 1);
    printf("\n");
    tr_write(s2, ((int16_t[]){-20,5,12,0,11,-11}), 0, 6);
    tr_write(s3, ((int16_t[]){-12,-18,-14,-10,5,-9,8,16,-6}), 0, 9);
    tr_delete_range(s3, 5, 3); //expect return True
    tr_insert(s1, s0, 0, 7, 3);
    tr_write(s1, ((int16_t[]){-10,-6,-7,18,2,-12,12,16,-15,-13,20,-17,17,1}), 0, 14);
    tr_write(s0, ((int16_t[]){17,-16,-11}), 0, 3);
    int16_t FAILING_READ[14];
    tr_read(s1, FAILING_READ, 0, 14);
    //expected [-10  -6  -7  18   2 -12  12  17 -16 -11  20 -17  17   1], actual [-10  -6  -7  18   2 -12  12  17 -16 -13  20 -17  17   1]!
    for (int i = 0; i < 14; i++) {
        printf("%d ", FAILING_READ[i]);
    }
    tr_destroy(s0);
    tr_destroy(s1);
    tr_destroy(s2);
    tr_destroy(s3);
}