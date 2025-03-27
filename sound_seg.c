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
    struct sound_seg_node* nodes; // Array of nodes
    size_t length;                // Current number of nodes
    size_t capacity;              // Total allocated size
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
    // Decrease refCount for any child nodes
    for (size_t i = 0; i < track->length; i++) {
        if (!track->nodes[i].isParent && track->nodes[i].A.child_data.parent) {
            track->nodes[i].A.child_data.parent->A.parent_data.refCount--;
        }
    }
    free(track->nodes);
    free(track);
}

size_t tr_length(struct sound_seg* track) {
    return track ? track->length : 0;
}

void tr_resize(struct sound_seg* track, size_t new_capacity) {
    if (!track || new_capacity <= track->capacity) return;
    struct sound_seg_node* new_nodes = realloc(track->nodes, new_capacity * sizeof(struct sound_seg_node));
    if (!new_nodes) return; // Handle allocation failure gracefully
    track->nodes = new_nodes;
    // Initialize new nodes to zeroed parent nodes
    for (size_t i = track->capacity; i < new_capacity; i++) {
        track->nodes[i].A.parent_data.sample = 0;
        track->nodes[i].A.parent_data.refCount = 0;
        track->nodes[i].isParent = true;
    }
    track->capacity = new_capacity;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest || pos >= track->length || len == 0) return;
    size_t available = track->length - pos;
    size_t to_copy = len < available ? len : available;
    for (size_t i = 0; i < to_copy; i++) {
        struct sound_seg_node* node = &track->nodes[pos + i];
        dest[i] = node->isParent ? node->A.parent_data.sample : *(node->A.child_data.parent_data_address);
    }
}

void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;
    size_t new_length = pos + len;
    if (new_length > track->capacity) {
        tr_resize(track, new_length * 2); // Grow by factor of 2
    }
    if (new_length > track->length) {
        track->length = new_length;
    }
    for (size_t i = 0; i < len; i++) {
        struct sound_seg_node* node = &track->nodes[pos + i];
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

    // Check if any parent nodes in range have references
    for (size_t i = pos; i < end; i++) {
        if (track->nodes[i].isParent && track->nodes[i].A.parent_data.refCount > 0) {
            return false;
        }
    }

    // Decrease refCount for child nodes being deleted
    for (size_t i = pos; i < end; i++) {
        if (!track->nodes[i].isParent && track->nodes[i].A.child_data.parent) {
            track->nodes[i].A.child_data.parent->A.parent_data.refCount--;
        }
    }

    // Shift remaining nodes
    if (end < track->length) {
        memmove(&track->nodes[pos], &track->nodes[end], (track->length - end) * sizeof(struct sound_seg_node));
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

    // Shift existing nodes in dest_track to make space
    if (destpos < dest_track->length) {
        memmove(&dest_track->nodes[destpos + len], &dest_track->nodes[destpos],
                (dest_track->length - destpos) * sizeof(struct sound_seg_node));
    }

    // Insert nodes as children referencing src_track's parents
    for (size_t i = 0; i < len; i++) {
        struct sound_seg_node* src_node = &src_track->nodes[srcpos + i];
        struct sound_seg_node* parent = src_node;
        while (!parent->isParent && parent->A.child_data.parent) {
            parent = parent->A.child_data.parent;
        }
        struct sound_seg_node* dest_node = &dest_track->nodes[destpos + i];
        dest_node->isParent = false;
        dest_node->A.child_data.parent = parent;
        dest_node->A.child_data.parent_data_address = &parent->A.parent_data.sample;
        parent->A.parent_data.refCount++;
    }
    dest_track->length = new_length > dest_track->length ? new_length : dest_track->length;
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

// wav_load and wav_save adjusted for array of nodes
void wav_load(const char* filename, struct sound_seg* track) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Error opening file\n");
        return;
    }
    fseek(file, OFFSET, SEEK_SET);
    uint32_t number_of_bytes;
    fread(&number_of_bytes, sizeof(uint32_t), 1, file);
    size_t sample_count = number_of_bytes / sizeof(int16_t);
    tr_resize(track, sample_count);
    fseek(file, OFFSET_TO_AUDIO_DATA, SEEK_SET);
    for (size_t i = 0; i < sample_count; i++) {
        fread(&track->nodes[i].A.parent_data.sample, sizeof(int16_t), 1, file);
        track->nodes[i].A.parent_data.refCount = 0;
        track->nodes[i].isParent = true;
    }
    track->length = sample_count;
    fclose(file);
}

void wav_save(const char* fname, struct sound_seg* track) {
    FILE* file = fopen(fname, "wb");
    if (!file) {
        printf("Error opening file\n");
        return;
    }
    union wav_header {
        struct {
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
        } fields;
        char bytes[OFFSET_TO_AUDIO_DATA];
    } header = {
        .fields = {
            .riff = "RIFF",
            .flength = track->length * sizeof(int16_t) + OFFSET_TO_AUDIO_DATA,
            .wave = "WAVE",
            .fmt = "fmt ",
            .chunk_size = 16,
            .format_tag = 1,
            .num_chans = 1,
            .sample_rate = 8000,
            .bytes_per_sec = 8000 * 2,
            .bytes_per_sample = 2,
            .bits_per_sample = 16,
            .data = "data",
            .dlength = track->length * sizeof(int16_t)
        }
    };
    fwrite(&header, sizeof(header), 1, file);
    int16_t* buffer = malloc(track->length * sizeof(int16_t));
    if (buffer) {
        tr_read(track, buffer, 0, track->length);
        fwrite(buffer, sizeof(int16_t), track->length, file);
        free(buffer);
    }
    fclose(file);
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
    tr_write(s2, ((int16_t[]){-20,5,12,0,11,-11}), 0, 6);
    tr_write(s3, ((int16_t[]){-12,-18,-14,-10,5,-9,8,16,-6}), 0, 9);
    tr_delete_range(s3, 5, 3); // Expect return True
    tr_insert(s1, s0, 0, 7, 3);
    tr_write(s1, ((int16_t[]){-10,-6,-7,18,2,-12,12,16,-15,-13,20,-17,17,1}), 0, 14);
    tr_write(s0, ((int16_t[]){17,-16,-11}), 0, 3);
    int16_t FAILING_READ[14];
    tr_read(s1, FAILING_READ, 0, 14);
    // Expected [-10 -6 -7 18 2 -12 12 16 -15 -13 20 -17 17 1]
    for (int i = 0; i < 14; i++) {
        printf("%d ", FAILING_READ[i]);
    }
    printf("\n");
    tr_destroy(s0);
    tr_destroy(s1);
    tr_destroy(s2);
    tr_destroy(s3);
    return 0;
}