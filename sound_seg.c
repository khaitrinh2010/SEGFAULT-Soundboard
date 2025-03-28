#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44
#pragma pack(push, 1)
struct sound_seg_node {
    union A {
        struct {
            int refCount;
            int16_t sample;
        } parent_data;
        struct {
            struct sound_seg_node* parent;
            int16_t* parent_data_address;
        } child_data;
    } A;
    bool isParent;
};
#pragma pack(pop)

struct sound_seg {
    struct sound_seg_node** nodes; // Array of pointers to nodes
    size_t length;
    size_t capacity;
};

struct sound_seg* tr_init(void) {
    struct sound_seg* track = malloc(sizeof(struct sound_seg));
    if (!track) return NULL;
    track->nodes = NULL;
    track->length = 0;
    track->capacity = 0;
    return track;
}

void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    for (size_t i = 0; i < track->length; i++) {
        free(track->nodes[i]);
    }
    free(track->nodes);
    free(track);
}

size_t tr_length(struct sound_seg* track) {
    return track ? track->length : 0;
}

void tr_resize(struct sound_seg* track, size_t new_capacity) {
    if (!track || new_capacity <= track->capacity) return;
    struct sound_seg_node** new_nodes = realloc(track->nodes, new_capacity * sizeof(struct sound_seg_node*));
    if (!new_nodes) return;
    track->nodes = new_nodes;
    for (size_t i = track->capacity; i < new_capacity; i++) {
        track->nodes[i] = malloc(sizeof(struct sound_seg_node));
        if (!track->nodes[i]) return; // Handle allocation failure
        track->nodes[i]->A.parent_data.sample = 0;
        track->nodes[i]->A.parent_data.refCount = 0;
        track->nodes[i]->isParent = true;
    }
    track->capacity = new_capacity;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest || pos >= track->length || len == 0) return;
    size_t available = track->length - pos;
    size_t to_copy = len < available ? len : available;
    for (size_t i = 0; i < to_copy; i++) {
        struct sound_seg_node* node = track->nodes[pos + i];
        dest[i] = node->isParent ? node->A.parent_data.sample : *(node->A.child_data.parent_data_address);
    }
}

void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;
    size_t new_length = pos + len;
    if (new_length > track->capacity) {
        tr_resize(track, new_length * 2);
    }
    if (new_length > track->length) {
        for (size_t i = track->length; i < new_length; i++) {
            track->nodes[i] = malloc(sizeof(struct sound_seg_node));
            if (!track->nodes[i]) return;
            track->nodes[i]->A.parent_data.sample = 0;
            track->nodes[i]->A.parent_data.refCount = 0;
            track->nodes[i]->isParent = true;
        }
        track->length = new_length;
    }
    for (size_t i = 0; i < len; i++) {
        struct sound_seg_node* node = track->nodes[pos + i];
        if (node->isParent) {
            node->A.parent_data.sample = src[i];
        } else {
            *(node->A.child_data.parent_data_address) = src[i];
        }
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || pos >= track->length || len == 0) return false;
    size_t end = pos + len > track->length ? track->length : pos + len;
    for (size_t i = pos; i < end; i++) {
        struct sound_seg_node* node = track->nodes[i];
        if (node->isParent && node->A.parent_data.refCount > 0) {
            return false;
        }
    }
    for (size_t i = pos; i < end; i++) {
        struct sound_seg_node* node = track->nodes[i];
        if (!node->isParent && node->A.child_data.parent) {
            node->A.child_data.parent->A.parent_data.refCount--;
        }
        free(node);
        track->nodes[i] = NULL;
    }
    if (end < track->length) {
        for (size_t i = pos; i < track->length - (end - pos); i++) {
            track->nodes[i] = track->nodes[i + (end - pos)]; // Shift pointers
        }
    }
    track->length -= end - pos;
    return true;
}
void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track, size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || srcpos + len > src_track->length || len == 0) return;
    size_t new_length = dest_track->length + len;
    if (new_length > dest_track->capacity) {
        tr_resize(dest_track, new_length * 2);
    }
    if (destpos < dest_track->length) {
        memmove(&dest_track->nodes[destpos + len], &dest_track->nodes[destpos],
                (dest_track->length - destpos) * sizeof(struct sound_seg_node*));
    }
    size_t offset = (src_track == dest_track && srcpos >= destpos) ? len : 0; // Only offset if inserting after srcpos
    for (size_t i = 0; i < len; i++) {
        size_t src_idx = srcpos + i + offset;
        if (src_idx >= src_track->length) return; // Bounds check
        struct sound_seg_node* src_node = src_track->nodes[src_idx];
        struct sound_seg_node* parent = src_node;
        while (!parent->isParent && parent->A.child_data.parent) {
            parent = parent->A.child_data.parent;
        }
        dest_track->nodes[destpos + i] = malloc(sizeof(struct sound_seg_node));
        if (!dest_track->nodes[destpos + i]) return;
        struct sound_seg_node* dest_node = dest_track->nodes[destpos + i];
        dest_node->isParent = false;
        dest_node->A.child_data.parent = parent;
        dest_node->A.child_data.parent_data_address = &parent->A.parent_data.sample;
        parent->A.parent_data.refCount++;
    }
    dest_track->length = new_length;
}
double compute_cross_correlation(const int16_t* target, const int16_t* ad, size_t len) {
    double sum_product = 0.0;
    double sum_ad_sq = 0.0;
    for (size_t i = 0; i < len; i++) {
        sum_product += (double)target[i] * (double)ad[i];
        sum_ad_sq += (double)ad[i] * (double)ad[i];
    }
    return sum_product / sum_ad_sq;
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


int main(int argc, char** argv) {
    struct sound_seg* s0 = tr_init();
    tr_write(s0, ((int16_t[]){1,9}), 0, 2);
    struct sound_seg* s1 = tr_init();
    tr_write(s1, ((int16_t[]){0,8,1,15}), 0, 4);
    struct sound_seg* s2 = tr_init();
    tr_write(s2, ((int16_t[]){-19,1,1,-12,-3,-13,18,-16,-5,-19,-18,14,-12,-4,19,17,-17,16,-20,4}), 0, 20);
    struct sound_seg* s3 = tr_init();
    tr_write(s3, ((int16_t[]){16,5}), 0, 2);
    struct sound_seg* s4 = tr_init();
    tr_write(s4, ((int16_t[]){}), 0, 0);
    tr_insert(s2, s3, 1, 18, 2);
    tr_write(s2, ((int16_t[]){-5,16,17,-8,-4,16,16,16,12,-3,16,12,18,16,8,-8,-12,10,16,16}), 0, 20);
    tr_write(s0, ((int16_t[]){0,-6,15}), 0, 3);
    tr_delete_range(s0, 1, 1); //expect return True
    tr_write(s3, ((int16_t[]){11,5,5,-2}), 0, 4);
    tr_delete_range(s1, 1, 1); //expect return True
    tr_write(s4, ((int16_t[]){10,10}), 0, 2);
    tr_delete_range(s4, 0, 1); //expect return True
    tr_write(s2, ((int16_t[]){-16,1,-2,19,0,14,6,-14,-7,0,-2,-11,-13,5,-2,2,-18,-2,-2,16}), 0, 20);
    tr_delete_range(s1, 1, 1); //expect return True
    tr_delete_range(s2, 5, 6); //expect return True
    tr_write(s1, ((int16_t[]){16,3,-16,-16,20}), 0, 5);
    tr_write(s3, ((int16_t[]){15}), 0, 1);
    tr_write(s3, ((int16_t[]){4,-19,-19,6,1}), 0, 5);
    int16_t FAILING_READ[14];
    tr_read(s2, FAILING_READ, 0, 14);
    //expected [-16   1  -2  19   0 -11 -13   5  -2   2 -18  -2 -19 -19], actual [-16   1  -2  19   0 -11 -13   5  -2   2 -18  -2  -2  16]!
    for (int i = 0; i < 14; i++) {
        printf("%d ", FAILING_READ[i]);
    }
    tr_destroy(s0);
    tr_destroy(s1);
    tr_destroy(s2);
    tr_destroy(s3);
    tr_destroy(s4);
}